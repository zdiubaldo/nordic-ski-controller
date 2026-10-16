"""Host framing tests; no radio, USB device, Bleak install or motion required."""
import importlib.util
import asyncio
from enum import IntEnum
try:
    from bleak.exc import BleakGATTProtocolError
except ImportError:
    BleakGATTProtocolError = None
import json
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location('ble_client', Path(__file__).parents[1]/'tools/test-ble-client.py')
client = importlib.util.module_from_spec(spec)
spec.loader.exec_module(client)


class FramingTests(unittest.TestCase):
    def test_minimum_mtu_and_bounds(self):
        text = 'v1 0123456789abcdef 3 1 targets 1 2'
        packets = list(client.encode(text, 42))
        self.assertTrue(all(len(packet) <= 20 for packet in packets))
        self.assertEqual(b''.join(packet[8:] for packet in packets).decode(), text)
        for bad in ('', 'x'*192):
            with self.assertRaises(ValueError):
                list(client.encode(bad, 1))

    def test_large_status_and_concurrent_channels(self):
        rx = client.Receiver()
        status = json.dumps({'boot_id': 9, 'revision': '10', 'padding': 'x'*2000}).encode()
        for offset in range(0, len(status), 12):
            data = struct.pack('<BBHHH', 1, 3, 1, len(status), offset)+status[offset:offset+12]
            result = rx.feed(data)
            if offset == 0:
                self.assertEqual(rx.feed(struct.pack('<BBHHH', 1, 2, 7, 11, 0)+b'{"ok":true}'), (2, 7, {'ok': True}))
        self.assertEqual(result, (3, 1, json.loads(status)))

    def test_out_of_order_and_recovery(self):
        rx = client.Receiver()
        with self.assertRaises(ValueError):
            rx.feed(struct.pack('<BBHHH', 1, 3, 1, 20, 8)+b'bad')
        self.assertEqual(rx.feed(struct.pack('<BBHHH', 1, 3, 2, 2, 0)+b'{}'), (3, 2, {}))
        for packet in [b'', struct.pack('<BBHHH', 2, 3, 2, 2, 0)+b'{}', struct.pack('<BBHHH', 1, 3, 2, 1, 0)+b'{}']:
            with self.assertRaises(ValueError):
                rx.feed(packet)


class BackpressureTests(unittest.IsolatedAsyncioTestCase):
    async def check_reassembly(self, error):
        class Link:
            def __init__(self):
                self.failed = False
                self.headers = []

            async def write_gatt_char(self, characteristic, packet, response):
                version, kind, ident, total, offset = client.HEADER.unpack_from(packet)
                self.headers.append((ident, offset))
                if offset + len(packet)-8 == total:
                    if not self.failed:
                        self.failed = True
                        raise error
                    subject.results[ident].set_result({'ok': True})

        link = Link()
        subject = client.Client(link)
        ident, result = await subject.request('v1 0123456789abcdef 8 1 heartbeat 0 0')
        self.assertTrue(result['ok'])
        self.assertEqual(sum(offset == 0 for _, offset in link.headers), 2)
        self.assertEqual({identifier for identifier, _ in link.headers}, {ident})

    async def test_structured_att_code_restarts_whole_message(self):
        class Code(IntEnum):
            INSUFFICIENT_RESOURCE = 17

        class ProtocolError(Exception):
            code = Code.INSUFFICIENT_RESOURCE

        await self.check_reassembly(ProtocolError('GATT Protocol Error: Insufficient Resource'))

    @unittest.skipIf(BleakGATTProtocolError is None, 'Bleak optional for radio-free CI')
    async def test_actual_bleak3_error(self):
        await self.check_reassembly(BleakGATTProtocolError(17))

    async def test_bounded_and_only_resource_errors_retry(self):
        class ProtocolError(Exception):
            def __init__(self, code):
                self.code = code

        class Link:
            def __init__(self, code):
                self.count = 0
                self.code = code

            async def write_gatt_char(self, *args, **kwargs):
                self.count += 1
                raise ProtocolError(self.code)

        for code, attempts in [(17, 4), (5, 1)]:
            link = Link(code)
            with self.assertRaises(ProtocolError):
                await client.Client(link).request('status')
            self.assertEqual(link.count, attempts)


class SnapshotTests(unittest.IsolatedAsyncioTestCase):
    @staticmethod
    def publish(subject, revision, boot=7):
        body = json.dumps({'boot_id': boot, 'revision': str(revision), 'workout': {'overridden': revision >= 101}}).encode()
        subject.notification(None, client.HEADER.pack(1, 3, revision, len(body), 0)+body)

    async def test_ignores_inflight_precommand_snapshot(self):
        subject = client.Client(None)
        subject.command_revision = 101

        async def request(_body):
            self.publish(subject, 100)
            return 1, {'ok': True}

        subject.request = request
        task = asyncio.create_task(subject.get_status())
        await asyncio.sleep(0)
        self.assertFalse(task.done(), 'Precommand snapshot incorrectly satisfied read')
        self.publish(subject, 101)
        result = await asyncio.wait_for(task, 1)
        self.assertTrue(result['workout']['overridden'])
        self.publish(subject, 99)
        self.assertEqual(subject.status['revision'], '101')

    async def test_special_reply_does_not_clear_command_barrier(self):
        class Link:
            async def write_gatt_char(self, _characteristic, packet, response):
                ident = client.HEADER.unpack_from(packet)[2]
                subject.results[ident].set_result({'ok': True})

        subject = client.Client(Link())
        subject.command_revision = 101
        await subject.request('status')
        self.assertEqual(subject.command_revision, 101)

    async def test_new_boot_rejected_instead_of_comparing_revisions(self):
        subject = client.Client(None)
        self.publish(subject, 100)

        async def request(_body):
            self.publish(subject, 101, boot=8)
            return 1, {'ok': True}

        subject.request = request
        with self.assertRaisesRegex(RuntimeError, 'restarted'):
            await subject.get_status()


class CleanupTests(unittest.IsolatedAsyncioTestCase):
    async def test_heartbeat_failure_still_attempts_stop_and_release(self):
        class FailingHeartbeat(client.Client):
            def __init__(self):
                self.actions = []

            async def get_status(self):
                return {'session_active': False, 'mode': 'idle', 'fault': 0, 'relays': {'mask': 0}}

            async def connect_service(self):
                pass

            async def command(self, action, a=0, b=0):
                self.actions.append(action)
                if action == 'stop':
                    raise RuntimeError('Simulated lost Stop result')

            async def heartbeats(self):
                raise RuntimeError('Simulated heartbeat failure')

        subject = FailingHeartbeat()
        with self.assertRaises(ExceptionGroup) as caught:
            await subject.exercise()
        self.assertIn('stop', subject.actions)
        self.assertIn('release', subject.actions)
        self.assertEqual(len(caught.exception.exceptions), 2)


if __name__ == '__main__':
    unittest.main()
