"""Deterministic application model using arbitrary simulation units."""

from dataclasses import dataclass
from enum import Enum
import math


class Mode(str, Enum):
    IDLE = "idle"
    RUNNING = "running"
    FAULT = "fault"


def number(value, name):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError(f"{name} must be a finite number")
    return float(value)


@dataclass(frozen=True)
class Snapshot:
    mode: Mode
    target_speed: float
    target_incline: float
    speed: float
    incline: float
    fault: str | None


class Controller:
    """Call tick regularly; the real firmware must own that loop independently."""

    def __init__(self, timeout=2.0):
        self.timeout = number(timeout, "timeout")
        if self.timeout <= 0:
            raise ValueError("timeout must be positive")
        self.mode = Mode.IDLE
        self.target_speed = self.target_incline = 0.0
        self.speed = self.incline = 0.0
        self.fault = None
        self._heartbeat = None
        self._time = None

    def snapshot(self):
        return Snapshot(self.mode, self.target_speed, self.target_incline,
                        self.speed, self.incline, self.fault)

    def _zero(self):
        self.target_speed = self.target_incline = 0.0
        self.speed = self.incline = 0.0

    def trip(self, reason):
        self._zero()
        self.mode = Mode.FAULT
        self.fault = str(reason)

    def stop(self):
        self._zero()
        if self.mode != Mode.FAULT:
            self.mode = Mode.IDLE

    def reset(self):
        # A real reset must also verify that all physical fault causes cleared.
        self._zero()
        self.mode = Mode.IDLE
        self.fault = None
        self._heartbeat = None

    def tick(self, now):
        now = number(now, "time")
        if now < 0 or (self._time is not None and now < self._time):
            raise ValueError("time must be nonnegative and monotonic")
        elapsed = 0.0 if self._time is None else now - self._time
        self._time = now
        if self.mode != Mode.RUNNING:
            return self.snapshot()
        if self._heartbeat is None or now - self._heartbeat >= self.timeout:
            self.trip("communication timeout")
            return self.snapshot()
        self.speed = self._approach(self.speed, self.target_speed, 10.0 * elapsed)
        self.incline = self._approach(self.incline, self.target_incline, 5.0 * elapsed)
        return self.snapshot()

    @staticmethod
    def _approach(current, target, step):
        return current + max(-step, min(step, target - current))

    def start(self, now):
        self.tick(now)
        if self.mode != Mode.IDLE:
            raise ValueError("start requires idle state")
        self._zero()
        self._heartbeat = float(now)
        self.mode = Mode.RUNNING

    def heartbeat(self, now):
        self.tick(now)
        if self.mode == Mode.RUNNING:
            self._heartbeat = float(now)

    def set_targets(self, speed, incline, now):
        self.tick(now)
        if self.mode != Mode.RUNNING:
            raise ValueError("setpoints require running state")
        speed = number(speed, "speed")
        incline = number(incline, "incline")
        if not (0 <= speed <= 100 and 0 <= incline <= 100):
            raise ValueError("setpoints must be within 0–100 simulation units")
        self.target_speed, self.target_incline = speed, incline
