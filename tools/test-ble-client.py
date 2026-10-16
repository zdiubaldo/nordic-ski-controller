#!/usr/bin/env python3
"""Nordic BLE diagnostic client. Default reads only; --exercise explicitly actuates.
Install bleak (and pyserial for --usb-pair). Never changes host Wi-Fi settings.
"""
import argparse
import asyncio
import contextlib
import json
import struct
import time
from pathlib import Path

BASE = '75e6{:04x}-6b92-4e3d-a347-8bc06c51d210'
SERVICE, INFO, COMMAND, RESULT, STATUS, CATALOG = [BASE.format(i) for i in range(6)]
HEADER = struct.Struct('<BBHHH')


def encode(text, ident, packet_size=20):
    body = text.encode('ascii')
    if not 0 < len(body) <= 191 or not 0 < ident <= 65535 or packet_size <= HEADER.size:
        raise ValueError('Invalid command size, ID or packet size')
    for offset in range(0, len(body), packet_size-HEADER.size):
        yield HEADER.pack(1, 1, ident, len(body), offset) + body[offset:offset+packet_size-HEADER.size]


def insufficient_resources(error):
    # Bleak 3 exposes the ATT enum through .code; its text no longer contains
    # hexadecimal 0x11. Preserve the older backend spelling as a fallback only
    # when no structured protocol code is available.
    code = getattr(error, 'code', None)
    if code is not None:
        return code == 0x11
    return '0x11' in str(error)


class Receiver:
    def __init__(self):
        self.pending = {}

    def feed(self, packet):
        if len(packet) <= HEADER.size:
            raise ValueError('Short notification')
        version, kind, ident, total, offset = HEADER.unpack_from(packet)
        body = packet[HEADER.size:]
        if version != 1 or kind not in (2, 3, 4) or not ident or not total or offset+len(body) > total:
            raise ValueError('Invalid notification header')
        key = kind  # Each characteristic carries one message at a time.
        if offset == 0:
            self.pending[key] = (ident, total, bytearray())
        previous = self.pending.get(key)
        if previous is None or previous[:2] != (ident, total) or len(previous[2]) != offset:
            self.pending.pop(key, None)
            raise ValueError('Missing, duplicate or out-of-order notification fragment')
        previous[2].extend(body)
        if len(previous[2]) != total:
            return None
        del self.pending[key]
        return kind, ident, json.loads(previous[2])


class Client:
    def __init__(self, bleak_client, packet_size=20):
        self.ble = bleak_client
        self.packet_size = packet_size
        self.rx = Receiver()
        self.ident = 0
        self.results = {}
        self.status = None
        self.boot_id = None
        self.boot_error = None
        self.command_revision = 0
        self.status_event = asyncio.Event()
        self.catalogs = {}
        self.catalog_event = asyncio.Event()
        self.token = None
        self.sequence = 0
        self.epoch = 0
        self.lock = asyncio.Lock()
        self.errors = []

    def notification(self, _characteristic, data):
        try:
            completed = self.rx.feed(data)
            if completed is None:
                return
            kind, ident, value = completed
            if kind == 2:
                future = self.results.get(ident)
                if future and not future.done():
                    future.set_result(value)
            elif kind == 3:
                if self.boot_id is None:
                    self.boot_id = value['boot_id']
                if value['boot_id'] != self.boot_id:
                    self.boot_error = RuntimeError('Controller restarted during BLE session; reconnect explicitly')
                elif self.status is None or int(value['revision']) > int(self.status['revision']):
                    self.status = value
                self.status_event.set()
            elif kind == 4:
                self.catalogs[ident] = value
                self.catalog_event.set()
        except (ValueError, UnicodeError) as error:
            self.errors.append(str(error))

    async def subscribe(self):
        for characteristic in (RESULT, STATUS, CATALOG):
            await self.ble.start_notify(characteristic, self.notification)

    async def request(self, body):
        # Serialize requests and result waits, while notifications run independently.
        async with self.lock:
            self.ident = self.ident % 65535 + 1
            ident = self.ident
            future = asyncio.get_running_loop().create_future()
            self.results[ident] = future
            try:
                for attempt in range(4):
                    try:
                        for packet in encode(body, ident, self.packet_size):
                            await self.ble.write_gatt_char(COMMAND, packet, response=True)
                        break
                    except Exception as error:
                        if attempt == 3 or not insufficient_resources(error):
                            raise
                        # A rejected write did not enqueue the command. Restart
                        # assembly at offset zero using the same ID and sequence.
                        await asyncio.sleep(.05)
                result = await asyncio.wait_for(future, 10)
                if not result.get('ok'):
                    raise RuntimeError('Command rejected: ' + str(result.get('reason')))
                # Special status/catalog replies omit revision: never erase the
                # barrier established by an accepted state-changing command.
                if 'revision' in result:
                    self.command_revision = max(self.command_revision, int(result['revision']))
                return ident, result
            finally:
                self.results.pop(ident, None)

    async def connect_service(self):
        _, result = await self.request('connect')
        self.token = result['token']
        self.epoch = result['control_epoch']

    async def command(self, action, a=0, b=0):
        if not self.token:
            raise RuntimeError('Service not registered')
        self.sequence += 1
        _, result = await self.request(f'v1 {self.token} {self.sequence} {self.epoch} {action} {a} {b}')
        self.epoch = result['control_epoch']
        return result

    async def get_status(self):
        # A fragmented notification already in flight may predate the last
        # accepted command. Wait for its revision barrier within this boot.
        required = self.command_revision
        if self.status is not None:
            required = max(required, int(self.status['revision'])+1)
        await self.request('status')
        deadline = time.monotonic()+10
        while True:
            self.status_event.clear()
            if self.boot_error is not None:
                raise self.boot_error
            if self.status is not None and int(self.status['revision']) >= required:
                return self.status
            await asyncio.wait_for(self.status_event.wait(), max(.01, deadline-time.monotonic()))

    async def get_catalog(self):
        ident, _ = await self.request('workouts')
        deadline = time.monotonic()+60
        while ident not in self.catalogs:
            self.catalog_event.clear()
            await asyncio.wait_for(self.catalog_event.wait(), max(.01, deadline-time.monotonic()))
        return self.catalogs.pop(ident)

    async def heartbeats(self):
        while True:
            await asyncio.sleep(.5)
            await self.command('heartbeat')

    async def exercise(self, full_workout=False):
        initial = await self.get_status()
        if initial['session_active'] or initial['mode'] == 'running' or initial.get('relays', {}).get('mask'):
            raise RuntimeError('Leave the controller stopped and release web ownership before exercising BLE')
        await self.connect_service()
        await self.command('claim')
        heartbeat = asyncio.create_task(self.heartbeats())

        async def pause(seconds):
            deadline = time.monotonic()+seconds
            while time.monotonic() < deadline:
                if heartbeat.done():
                    heartbeat.result()
                    raise RuntimeError('Heartbeat task ended unexpectedly')
                await asyncio.sleep(min(.1, max(0, deadline-time.monotonic())))

        try:
            if initial['mode'] == 'fault':
                await self.command('reset')
            await self.command('start')
            await pause(.8)
            for speed, grade in [(1, 0), (.5, 0), (.5, 2), (.5, 1)]:
                await self.command('targets', speed, grade)
                await pause(.8)
                state = await self.get_status()
                assert state['requested_speed_mps'] == speed and state['requested_grade_percent'] == grade
            await self.command('stop')
            await pause(.8)
            if full_workout:
                await self.command('w:hills', 1, 100)
                # Apply early in the six-second first segment so an entire
                # status snapshot can arrive before its natural boundary.
                await pause(.8)
                await self.command('targets', 1, 4)
                overridden = await self.get_status()
                assert overridden['workout']['overridden']
                initial_segment = overridden['workout']['segment']
                saw_next_segment = False
                deadline = time.monotonic()+65
                while time.monotonic() < deadline:
                    await pause(.5)
                    state = await self.get_status()
                    assert state['fault'] == 0, 'Workout faulted'
                    if state['workout']['active'] and state['workout']['segment'] != initial_segment:
                        assert not state['workout']['overridden'], 'Manual override persisted into next segment'
                        saw_next_segment = True
                    if state['workout']['complete']:
                        break
                assert saw_next_segment, 'Did not observe next-segment override reset'
                assert state['mode'] == 'idle' and state['fault'] == 0
                assert state['workout']['complete'] and not state['workout']['active']
                assert state['workout']['elapsed_ms'] == state['workout']['duration_ms'] == 60000
            return {'numeric_targets': 'passed', 'full_workout': 'passed' if full_workout else 'not_requested'}
        finally:
            cleanup_errors = []
            heartbeat.cancel()
            try:
                await heartbeat
            except asyncio.CancelledError:
                pass
            except Exception as error:
                cleanup_errors.append(error)
            # Attempt both Stop and release even if heartbeat/connection/Stop
            # failed. Each error remains visible rather than skipping cleanup.
            for action in ('stop', 'release'):
                try:
                    await self.command(action)
                    await asyncio.sleep(.8)
                except Exception as error:
                    cleanup_errors.append(error)
            try:
                final = await self.get_status()
                if final['mode'] == 'running' or final['session_active'] or final.get('relays', {}).get('mask'):
                    raise RuntimeError('Final stopped/released/relays-off check failed')
            except Exception as error:
                cleanup_errors.append(error)
            if cleanup_errors:
                raise ExceptionGroup('BLE heartbeat/cleanup failures', cleanup_errors)


def usb_pair(port):
    import serial
    connection = serial.Serial(port=None, baudrate=115200, timeout=.1, write_timeout=1)
    connection.dtr = True
    connection.rts = True
    connection.port = port
    connection.open()
    try:
        connection.write(b'991 ble-pair\n')
        pending = bytearray()
        deadline = time.monotonic()+5
        while time.monotonic() < deadline:
            pending.extend(connection.read(1024))
            while b'\n' in pending:
                line, _, pending = pending.partition(b'\n')
                if line.startswith(b'@nordic 991 '):
                    result = json.loads(line[len(b'@nordic 991 '):])
                    if not result.get('ok'):
                        raise RuntimeError('ESP pairing window rejected: ' + result.get('error', 'unknown'))
                    return int(result['passkey'])
        raise RuntimeError('USB pairing response timed out')
    finally:
        connection.close()


async def pairing_agent(passkey, address):
    # Linux-only temporary BlueZ keyboard agent; no passkey is printed or saved.
    from dbus_fast import BusType
    from dbus_fast.aio import MessageBus
    from dbus_fast.service import ServiceInterface, method
    from dbus_fast.errors import DBusError
    expected = '/dev_' + address.replace(':', '_').upper()

    class Agent(ServiceInterface):
        def __init__(self):
            super().__init__('org.bluez.Agent1')

        @method()
        def RequestPasskey(self, device: 'o') -> 'u':
            if not device.upper().endswith(expected.upper()):
                raise DBusError('org.bluez.Error.Rejected', 'Unexpected device')
            return passkey

        @method()
        def Cancel(self):
            pass

        @method()
        def Release(self):
            pass

        @method()
        def RequestConfirmation(self, device: 'o', code: 'u'):
            raise DBusError('org.bluez.Error.Rejected', 'Passkey entry required')

        @method()
        def RequestAuthorization(self, device: 'o'):
            raise DBusError('org.bluez.Error.Rejected', 'Unexpected pairing method')

        @method()
        def AuthorizeService(self, device: 'o', uuid: 's'):
            if not device.upper().endswith(expected.upper()):
                raise DBusError('org.bluez.Error.Rejected', 'Unexpected device')

    bus = await MessageBus(bus_type=BusType.SYSTEM).connect()
    path = '/nordic/ble_test_agent'
    bus.export(path, Agent())
    introspection = await bus.introspect('org.bluez', '/org/bluez')
    manager = bus.get_proxy_object('org.bluez', '/org/bluez', introspection).get_interface('org.bluez.AgentManager1')
    await manager.call_register_agent(path, 'KeyboardOnly')
    await manager.call_request_default_agent(path)
    return bus, manager, path


async def main(args):
    from bleak import BleakClient, BleakScanner
    if args.address:
        device = await BleakScanner.find_device_by_address(args.address, timeout=15)
    else:
        found = await BleakScanner.discover(timeout=8, return_adv=True)
        candidates = [device for device, adv in found.values() if SERVICE in adv.service_uuids]
        if len(candidates) != 1:
            raise RuntimeError(f'Found {len(candidates)} Nordic devices; use --address to choose explicitly')
        device = candidates[0]
    if device is None:
        raise RuntimeError('Nordic BLE device not found')
    agent = None
    report = {'device': device.address, 'mode': 'exercise' if args.exercise else 'read_only'}
    if args.usb_pair:
        code = await asyncio.to_thread(usb_pair, args.usb_pair)
        agent = await pairing_agent(code, device.address)
        del code
    try:
        async with BleakClient(device, pair=bool(args.usb_pair or args.pair), timeout=60) as connection:
            client = Client(connection, args.packet_size)
            report['info'] = json.loads(await connection.read_gatt_char(INFO))
            report['reported_mtu'] = connection.mtu_size
            report['command_packet_size'] = args.packet_size
            await client.subscribe()
            report['initial'] = await client.get_status()
            report['workout_count'] = len(await client.get_catalog())
            if args.exercise:
                report['exercise'] = await client.exercise(args.full_workout)
            report['final'] = await client.get_status()
            report['framing_errors'] = client.errors
            if client.errors:
                raise RuntimeError('Notification framing errors: ' + '; '.join(client.errors))
    finally:
        if agent:
            bus, manager, path = agent
            with contextlib.suppress(Exception):
                await manager.call_unregister_agent(path)
            bus.disconnect()
    # No tokens, passkeys, raw command payloads or pairing exchanges in reports.
    if args.report:
        Path(args.report).write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address')
    parser.add_argument('--pair', action='store_true', help='Use OS pairing prompt (open ESP pairing window first)')
    parser.add_argument('--usb-pair', metavar='PORT', help='Linux: open ESP pairing window over USB and provide temporary BlueZ agent')
    parser.add_argument('--exercise', action='store_true', help='Explicitly actuate Start/Stop, speed and incline relays')
    parser.add_argument('--full-workout', action='store_true', help='With --exercise, run a complete one-minute hills workout')
    parser.add_argument('--packet-size', type=int, default=20, choices=range(20, 245), metavar='20..244', help='ATT write length; default tests minimum-MTU-sized writes')
    parser.add_argument('--report')
    arguments = parser.parse_args()
    if arguments.full_workout and not arguments.exercise:
        parser.error('--full-workout requires --exercise')
    asyncio.run(main(arguments))
