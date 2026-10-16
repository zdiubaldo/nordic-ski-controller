# Generic hardware boundary

**Unloaded relay option:** `CONFIG_NORDIC_RELAY_CONTROL` additionally sends accepted session requests to configurable physical relay outputs. The motion feedback described below remains a test fixture. See [relay mapping](relay-driver.md#existing-controls-on-the-unloaded-board) for behavior and limitations.

The `motion` ESP-IDF component is independent of relay wiring, a particular motor drive, and the web transport. It has no ESP-IDF-specific dependencies and can also run as native C. The current application and software-test webpage use it through the shared control service and memory driver only; they do **not** connect it to physical outputs. Nothing in this component opens GPIO, I2C, serial, or network devices.

## Contract

A future driver implements four operations:

| Operation | Responsibility |
| --- | --- |
| `read` | Return explicitly known/unknown measurements, readiness, interlock state, fault state, upper/lower travel limits, and the monotonic sample timestamp. |
| `enable` | Accept operation at zero roller demand while holding the current lift position. Never replay an earlier target. |
| `targets` | Accept bounded speed in m/s and absolute incline in percent grade, after the generic layer applies configured slew rates. |
| `stop` | Request the machine-specific roller stop and lift hold for the supplied reason. Do not interpret this as lowering the lift or turning every relay off. |

Successful callbacks acknowledge requests; they do not prove actual motion or a physical stop. `stop_acknowledged` means the driver accepted Stop. Feedback is separate. A stop failure latches a driver fault; its cause must be handled by the future machine implementation and independent safety circuit.

Use a single owning task. Transport handlers should queue commands to it. Drivers must have bounded execution time compatible with the configured tick deadline. Pass the same monotonic microsecond clock to feedback and the runner.

## Configuration and lifecycle

1. Initialize a driver and `motion_t`. Initialization makes no driver calls and leaves motion unconfigured.
2. Supply explicit commissioning configuration: maximum speed; minimum/maximum incline; speed acceleration/deceleration; incline change rate; feedback and command timeouts; maximum tick gap. No machine defaults are supplied. Invalid settings are rejected atomically. Configuration cannot change while running or faulted.
3. Inject/read fresh feedback and explicitly enable. Enable requires ready and healthy interlock, no driver fault, known noncontradictory travel limits, known finite speed/position within configured bounds, and a stationary roller. Initial incline demand comes from measured position, never a guessed zero.
4. Send targets and call `motion_tick` regularly. Valid target commands renew the command deadline. Ticks and rejected commands do not. Ramping limits changes to the last acknowledged setpoint; it is not a measured acceleration guarantee.
5. Stop cancels running without issuing an incline-zero target. Timeouts, stale/unknown feedback, interlock loss, blocked travel direction, clock errors, and driver errors request Stop and latch a fault.
6. Reset requires acknowledged Stop, fresh healthy feedback, and a stationary roller. Reset does not enable or resume. The next explicit enable starts with zero speed demand and measured incline.

This initial policy deliberately requires known speed and incline before enabling. Machines lacking such sensors will need an explicit reviewed homing/position-estimation or other startup policy; the API does not quietly substitute zero for missing measurements. A physical driver must implement independent limit enforcement and the correct stopping sequence. Neither this library nor a wireless command replaces emergency-stop hardware.

## In-memory driver

`motion_memory` records reads, enables, target writes, stops, and the last command values. Feedback stays unknown until supplied explicitly. Set `fail_read`, `fail_enable`, `fail_targets`, or `fail_stop` to inject failures. Writing a target never updates feedback automatically: this is a command recorder, not a motor simulation.

`tests/motion_tests.c` shows a complete setup with deliberately artificial limits and injected timestamped measurements. Its six fast groups cover unconfigured/unknown startup, ramping and lift holding, watchdog/reset behavior, feedback/travel limits, driver/clock errors, and rejected requests. They advance timestamps immediately and never wait for a real workout.

## Shared service integration

The webpage and workouts now reach `motion` through [the shared control service](control-service.md), using the memory driver. HTTP and future BLE adapters share command processing, ownership, and fault handling. A physical adapter, approved commissioning values, feedback policy, and machine-specific stopping behavior are still required before real movement can be enabled.
