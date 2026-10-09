import unittest

from simulator.controller import Controller, Mode


class ControllerTests(unittest.TestCase):
    def test_boot_and_restart_have_zero_targets(self):
        c = Controller()
        self.assertEqual(c.snapshot().mode, Mode.IDLE)
        c.start(0)
        c.set_targets(20, 10, 0)
        c.stop()
        c.start(1)
        self.assertEqual((c.target_speed, c.target_incline, c.speed, c.incline), (0, 0, 0, 0))

    def test_ramps_and_clamps_to_targets(self):
        c = Controller()
        c.start(0)
        c.set_targets(12, 6, 0)
        c.heartbeat(1)
        self.assertEqual((c.speed, c.incline), (10, 5))
        c.heartbeat(2)
        self.assertEqual((c.speed, c.incline), (12, 6))
        c.set_targets(0, 0, 2)
        c.heartbeat(3)
        self.assertEqual((c.speed, c.incline), (2, 1))

    def test_late_heartbeat_latches_fault(self):
        c = Controller()
        c.start(0)
        c.set_targets(20, 10, 0)
        c.heartbeat(2)
        self.assertEqual(c.mode, Mode.FAULT)
        self.assertEqual((c.speed, c.incline, c.target_speed), (0, 0, 0))
        c.heartbeat(3)
        c.stop()
        self.assertEqual(c.mode, Mode.FAULT)
        with self.assertRaises(ValueError):
            c.start(3)
        c.reset()
        self.assertEqual(c.mode, Mode.IDLE)
        c.start(4)
        self.assertEqual(c.target_speed, 0)

    def test_late_setpoint_cannot_bypass_timeout(self):
        c = Controller()
        c.start(0)
        with self.assertRaises(ValueError):
            c.set_targets(50, 50, 2)
        self.assertEqual(c.mode, Mode.FAULT)

    def test_invalid_setpoints_do_not_partially_update(self):
        c = Controller()
        c.start(0)
        c.set_targets(10, 5, 0)
        for bad in (float('nan'), float('inf'), -1, 101, True, '10', None):
            with self.subTest(bad=bad):
                with self.assertRaises(ValueError):
                    c.set_targets(40, bad, 0)
                with self.assertRaises(ValueError):
                    c.set_targets(bad, 40, 0)
                self.assertEqual((c.target_speed, c.target_incline), (10, 5))

    def test_idle_rejects_setpoints(self):
        c = Controller()
        with self.assertRaises(ValueError):
            c.set_targets(10, 5, 0)

    def test_time_validation(self):
        c = Controller()
        c.start(1)
        for bad in (0, -1, float('nan'), float('inf'), True):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                c.tick(bad)

    def test_timeout_validation(self):
        for bad in (0, -1, float('nan'), True):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                Controller(bad)


if __name__ == '__main__':
    unittest.main()
