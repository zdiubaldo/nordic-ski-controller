#!/usr/bin/env python3
"""Check the virtual BLE adapter's real C backend without Android or hardware."""
import unittest
from types import SimpleNamespace
from simulator import Backend, Simulator, HEADER


class BackendTest(unittest.IsolatedAsyncioTestCase):
    async def test_ble_identity_observer_stop_and_stale_epoch(self):
        backend = Backend()
        await backend.start()
        try:
            initial = await backend.call('status')
            self.assertFalse(initial['physical_outputs'])
            self.assertEqual(initial['driver']['type'], 'memory')
            owner = '1111111111111111'
            observer = '2222222222222222'
            await backend.call('register ' + owner)
            await backend.call('register ' + observer)
            epoch = initial['control_epoch']
            claimed = await backend.call(f'command v1 {owner} 1 {epoch} claim 0 0')
            self.assertTrue(claimed['ok'])
            self.assertEqual(claimed['owner_transport'], 2)
            started = await backend.call(f'command v1 {owner} 2 {epoch} start 0 0')
            self.assertEqual(started['mode'], 'running')
            denied = await backend.call(f'command v1 {observer} 1 {epoch} claim 0 0')
            self.assertEqual(denied['command_result']['reason'], 'control_in_use')
            stopped = await backend.call(f'command v1 {observer} 2 {epoch} stop 0 0')
            self.assertTrue(stopped['ok'])
            self.assertEqual(stopped['mode'], 'idle')
            stale = await backend.call(f'command v1 {owner} 3 {epoch} start 0 0')
            self.assertEqual(stale['command_result']['reason'], 'stale_epoch')
            self.assertEqual(stale['mode'], 'idle')
        finally:
            await backend.close()


class SubscriptionTest(unittest.IsolatedAsyncioTestCase):
    async def test_subscription_starts_with_complete_message(self):
        enabled = False
        packets = []
        async def notify(connection, characteristic, packet):
            packets.append(packet)
        device = SimpleNamespace(
            gatt_server=SimpleNamespace(read_cccd=lambda *_: bytes([int(enabled),0])),
            notify_subscriber=notify)
        connection=SimpleNamespace(handle=1,att_mtu=23)
        sim=Simulator(device,None,list(range(6)))
        sim.connections[1]=connection
        await sim.send(connection,3,1,{'mode':'idle','padding':'x'*40})
        self.assertEqual(packets,[])
        enabled=True
        await sim.send(connection,3,2,{'mode':'idle','padding':'x'*40})
        self.assertGreater(len(packets),1)
        self.assertEqual(HEADER.unpack_from(packets[0])[-1],0)
        offsets=[HEADER.unpack_from(packet)[-1] for packet in packets]
        self.assertEqual(offsets,list(range(0,sum(len(p)-8 for p in packets),12)))


if __name__ == '__main__':
    unittest.main()
