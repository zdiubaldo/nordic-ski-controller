#!/usr/bin/env python3
"""Virtual-radio-only Nordic peripheral backed by the real C memory controller.
Never opens USB, host Bluetooth, Wi-Fi interfaces, or physical relays.
"""
import argparse
import asyncio
import json
import secrets
import struct
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LOCAL = ROOT / '.local' / 'ble-simulator'
SOCKET = LOCAL / 'control.sock'
UUIDS = [f'75e6000{i}-6b92-4e3d-a347-8bc06c51d210' for i in range(6)]
HEADER = struct.Struct('<BBHHH')


class Backend:
    def __init__(self):
        self.lock = asyncio.Lock()
        self.process = None

    async def start(self):
        self.process = await asyncio.create_subprocess_exec(
            str(ROOT / '.local/ble-sim-controller'), '--ble',
            stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE)

    async def call(self, line):
        async with self.lock:
            self.process.stdin.write((line + '\n').encode())
            await self.process.stdin.drain()
            data = await asyncio.wait_for(self.process.stdout.readline(), 3)
            if not data:
                raise RuntimeError('Memory controller exited')
            return json.loads(data)

    async def close(self):
        if self.process and self.process.returncode is None:
            self.process.terminate()
            await self.process.wait()


class Simulator:
    def __init__(self, device, backend, characteristics):
        self.device, self.backend, self.chars = device, backend, characteristics
        self.connections, self.sessions, self.fragments, self.locks = {}, {}, {}, {}
        self.status_id = 0
        self.observer = None
        self.events = LOCAL / 'events.jsonl'

    def log(self, event, **fields):
        with self.events.open('a') as stream:
            stream.write(json.dumps({'time': time.time(), 'event': event, **fields}) + '\n')

    def connected(self, connection):
        self.connections[connection.handle] = connection
        self.log('connected')
        connection.on('disconnection', lambda reason: asyncio.create_task(self.disconnected(connection)))
        connection.on('pairing', lambda *args: self.log('paired', authenticated=connection.authenticated))

    async def disconnected(self, connection):
        handle = connection.handle
        self.connections.pop(handle, None)
        self.fragments.pop(handle, None)
        session = self.sessions.pop(handle, None)
        if session:
            await self.backend.call(f'command v1 {session["token"]} {session["sequence"]+1} 0 disconnect 0 0')
        self.log('disconnected')

    async def send(self, connection, kind, ident, value):
        body = json.dumps(value, separators=(',', ':')).encode()
        if len(body) > 65535:
            raise ValueError('Notification too large')
        # Separate result, state and catalog streams can interleave, as on ESP.
        lock = self.locks.setdefault((connection.handle, kind), asyncio.Lock())
        async with lock:
            # Begin only at a message boundary after CCCD subscription. Otherwise
            # subscribing mid-stream would deliver an orphaned continuation.
            if not self.device.gatt_server.read_cccd(connection, self.chars[kind+1])[0] & 1:
                return
            chunk = min(connection.att_mtu - 3, 244) - 8
            for offset in range(0, len(body), chunk):
                if connection.handle not in self.connections:
                    return
                packet = HEADER.pack(1, kind, ident, len(body), offset) + body[offset:offset+chunk]
                await self.device.notify_subscriber(connection, self.chars[kind+1], packet)
                await asyncio.sleep(.001)

    async def write(self, connection, packet):
        from bumble.att import ATT_Error, ATT_INVALID_ATTRIBUTE_LENGTH_ERROR
        try:
            if len(packet) <= 8:
                raise ValueError()
            version, kind, ident, total, offset = HEADER.unpack_from(packet)
            if version != 1 or kind != 1 or not ident or not 0 < total <= 191 or offset+len(packet)-8 > total:
                raise ValueError()
            if offset == 0:
                self.fragments[connection.handle] = (ident, total, bytearray(), time.monotonic())
            pending = self.fragments[connection.handle]
            if pending[:2] != (ident, total) or len(pending[2]) != offset or time.monotonic()-pending[3] > 2:
                raise ValueError()
            pending[2].extend(packet[8:])
            if len(pending[2]) != total:
                return
            del self.fragments[connection.handle]
            body = pending[2].decode('ascii')
            if any(ord(c) < 32 or ord(c) > 126 for c in body):
                raise ValueError()
        except (ValueError, KeyError, UnicodeError):
            self.fragments.pop(connection.handle, None)
            raise ATT_Error(ATT_INVALID_ATTRIBUTE_LENGTH_ERROR)
        asyncio.create_task(self.request(connection, ident, body))

    async def request(self, connection, ident, body):
        handle = connection.handle
        if body == 'connect':
            if handle not in self.sessions:
                token = secrets.token_hex(8)
                snapshot = await self.backend.call('register ' + token)
                self.sessions[handle] = {'token': token, 'sequence': 0, 'client_id': snapshot['command_result']['client_id']}
            session = self.sessions[handle]
            snapshot = await self.backend.call('status')
            reply = dict(ok=True, protocol_version=1, token=session['token'], client_id=session['client_id'], control_epoch=snapshot['control_epoch'])
            self.log('registered', client_id=session['client_id'])
        elif body in ('status', 'workouts'):
            reply = {'ok': True}
        else:
            parts = body.split()
            session = self.sessions.get(handle)
            if len(parts) != 7 or not session or parts[1] != session['token']:
                reply = {'ok': False, 'reason': 'unauthorized'}
            else:
                snapshot = await self.backend.call('command ' + body)
                session['sequence'] = max(session['sequence'], int(parts[2]))
                result = snapshot['command_result']
                reply = dict(ok=snapshot['ok'], reason=result['reason'], client_id=result['client_id'],
                             sequence=result['sequence'], control_epoch=snapshot['control_epoch'], revision=snapshot['revision'])
                if parts[4] != 'heartbeat':
                    self.log('command', action=parts[4], accepted=reply['ok'], reason=reply['reason'], mode=snapshot['mode'], owner=snapshot['owner_client_id'])
        await self.send(connection, 2, ident, reply)
        if body == 'workouts':
            catalog = await self.backend.call('workouts')
            await self.send(connection, 4, ident, catalog)
            self.log('catalog', count=len(catalog))

    async def periodic(self):
        while True:
            if self.observer:
                await self.observer_action('heartbeat')
            snapshot = await self.backend.call('status')
            self.status_id = self.status_id % 65535 + 1
            await asyncio.gather(*(self.send(c, 3, self.status_id, snapshot) for c in list(self.connections.values()) if c.is_encrypted and c.authenticated))
            await asyncio.sleep(.2)

    async def observer_action(self, action):
        observer = self.observer
        if not observer:
            return {'ok': False, 'reason': 'observer_not_registered'}
        observer['sequence'] += 1
        sequence = observer['sequence']
        snapshot = await self.backend.call('status')
        return await self.backend.call(f'command v1 {observer["token"]} {sequence} {snapshot["control_epoch"]} {action} 0 0')

    async def control(self, reader, writer):
        try:
            action = (await reader.readline()).decode().strip()
            if action == 'status':
                result = await self.backend.call('status')
            elif action == 'observer_start':
                if not self.observer:
                    self.observer = {'token': secrets.token_hex(8), 'sequence': 0}
                    await self.backend.call('register ' + self.observer['token'])
                result = await self.observer_action('claim')
                if result['ok']:
                    result = await self.observer_action('start')
            elif action == 'observer_release':
                result = await self.observer_action('release') if self.observer else {'ok': True}
                if self.observer:
                    await self.observer_action('disconnect')
                    self.observer = None
            elif action == 'drop':
                await asyncio.gather(*(c.disconnect() for c in list(self.connections.values())))
                result = {'ok': True}
            else:
                result = {'ok': False, 'reason': 'unknown_simulator_action'}
            writer.write(json.dumps(result).encode()+b'\n')
            await writer.drain()
        finally:
            writer.close()
            await writer.wait_closed()


async def main(args):
    LOCAL.mkdir(parents=True, exist_ok=True)
    if args.control:
        reader, writer = await asyncio.open_unix_connection(SOCKET)
        writer.write((args.control+'\n').encode()); await writer.drain()
        print((await reader.readline()).decode().strip())
        writer.close(); await writer.wait_closed()
        return
    # Only virtual-radio transports are accepted. Physical adapters cannot be selected.
    if not args.transport.startswith('android-netsim') or 'mode=controller' in args.transport:
        raise ValueError('Simulator only accepts android-netsim virtual radio transport')
    from bumble.device import Device, DeviceConfiguration
    from bumble.core import AdvertisingData, UUID
    from bumble.gatt import Characteristic, CharacteristicValue, Service
    from bumble.pairing import PairingConfig, PairingDelegate
    from bumble.keys import JsonKeyStore
    from bumble.transport import open_transport

    class SimulatedDisplay(PairingDelegate):
        async def generate_passkey(self):
            return 123456
        async def display_number(self, number, digits=6):
            print('Virtual controller pairing code: 123456 (simulation only)', flush=True)

    backend = Backend()
    await backend.start()
    try:
        initial = await backend.call('status')
        async with await open_transport(args.transport) as transport:
            config = DeviceConfiguration.from_dict({'name': 'Nordic Ski Simulator', 'address': 'F0:F1:F2:F3:F4:F6', 'keystore': f'JsonKeyStore:{LOCAL / "bonds-random.json"}'})
            device = Device.from_config_with_hci(config, transport.source, transport.sink)
            # Netsim assigns a new public address on restart; keep the bond
            # namespace tied to this simulator instead of that transient address.
            device.keystore = JsonKeyStore("nordic-simulator", str(LOCAL / "bonds-random.json"))
            device.pairing_config_factory = lambda _: PairingConfig(sc=True, mitm=True, bonding=True, identity_address_type=PairingConfig.AddressType.RANDOM,
                delegate=SimulatedDisplay(io_capability=PairingDelegate.IoCapability.DISPLAY_OUTPUT_ONLY))
            chars = [None]
            info = json.dumps({'protocol_version': 1, 'framing': 1, 'boot_id': initial['boot_id'], 'device_id': 'simulator-only'}).encode()
            chars.append(Characteristic(UUIDS[1], Characteristic.Properties.READ, Characteristic.READABLE, info))
            chars.append(Characteristic(UUIDS[2], Characteristic.Properties.WRITE,
                Characteristic.WRITEABLE | Characteristic.WRITE_REQUIRES_ENCRYPTION | Characteristic.WRITE_REQUIRES_AUTHENTICATION,
                CharacteristicValue()))
            for uuid in UUIDS[3:]:
                chars.append(Characteristic(uuid, Characteristic.Properties.NOTIFY, 0, b''))
            sim = Simulator(device, backend, chars)
            chars[2].value = CharacteristicValue(write=sim.write)
            device.add_service(Service(UUIDS[0], chars[1:]))
            device.on('connection', sim.connected)
            await device.power_on()
            adv = bytes(AdvertisingData([(AdvertisingData.FLAGS, b'\x06'),
                (AdvertisingData.COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS, bytes(UUID(UUIDS[0])))]))
            scan = bytes(AdvertisingData([(AdvertisingData.COMPLETE_LOCAL_NAME, b'Nordic Ski Simulator')]))
            await device.start_advertising(advertising_data=adv, scan_response_data=scan, auto_restart=True)
            SOCKET.unlink(missing_ok=True)
            server = await asyncio.start_unix_server(sim.control, SOCKET)
            SOCKET.chmod(0o600)
            print('Virtual Nordic Ski controller ready. Pair with 123456. No physical hardware.', flush=True)
            periodic = asyncio.create_task(sim.periodic())
            serving = asyncio.create_task(server.serve_forever())
            terminated = asyncio.ensure_future(transport.source.terminated)
            try:
                done, _ = await asyncio.wait([serving, periodic, terminated], return_when=asyncio.FIRST_COMPLETED)
                for task in done:
                    task.result()
            finally:
                periodic.cancel(); serving.cancel(); terminated.cancel()
                await asyncio.gather(periodic, serving, terminated, return_exceptions=True)
                server.close(); await server.wait_closed(); SOCKET.unlink(missing_ok=True)
    finally:
        await backend.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--transport', default='android-netsim')
    parser.add_argument('--control', choices=['status','observer_start','observer_release','drop'])
    try:
        asyncio.run(main(parser.parse_args()))
    except KeyboardInterrupt:
        pass
