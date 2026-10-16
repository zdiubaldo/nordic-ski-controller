# Relay expander driver

The board's [Waveshare documentation](https://www.waveshare.com/wiki/ESP32-S3-ETH-8DI-8RO) identifies a TCA9554 expander at address 0x20. Register behavior is based on the [TI TCA9554 datasheet, section 8.6](https://www.ti.com/lit/ds/symlink/tca9554.pdf).

`firmware/components/relay` contains a C driver and an ESP-IDF I2C adapter. The opt-in `relay_control` component now binds it to the unloaded Waveshare board as described below. Default builds leave physical outputs disabled.

Initialization writes the inactive output latch and verifies it before configuring the eight pins as outputs. It then verifies configuration. Each subsequent output update checks configuration and reads back the output latch. Any transaction error or mismatch invalidates the driver; another output request is rejected until explicit reinitialization. Reinitialization requests all relays inactive and never restores the old command.

`commanded_mask` is the last successfully acknowledged logical command, not measured relay contact state. When `ready` is false, hardware state is unknown. A failed transaction may have reached the device. Register readback cannot prove contact operation or guarantee shutdown after bus failure. This driver does not provide the machine's independent stopping function. Expander reset detection is performed during writes; the opt-in relay task checks it periodically.

The ESP-IDF adapter bounds each I2C transaction to 50 ms. Multiple transactions can take longer than the 20 ms control tick, so do not call it synchronously from that loop without completing the task/timing design. No machine stopping deadline has been selected.

Tests substitute only the register-access functions, while executing the actual driver C code. They exercise startup order, eight output bits, both polarities, failures at every initialization/update transaction, readback mismatches, missing callbacks, and expander configuration loss. The same tests are compiled into the onboard Unity image. This is not a hardware emulator.

Before connecting the driver: verify schematic/revision, I2C pins, address, relay polarity and mapping; test power-on and MCU-only reset with unloaded contacts; measure timing; add runtime health supervision and fault propagation to control; then design machine-level relay assignments and interlocks from drive/lift documentation.

## Existing controls on the unloaded board

The opt-in `CONFIG_NORDIC_RELAY_CONTROL` mode connects accepted Start/Stop, numeric requested settings, workouts, and discrete speed/incline button requests to the relay task. Numeric settings and workouts use the original direction-of-change mapping. This is an unloaded bench mapping, not a commissioned motor/lift adapter. Motion feedback remains explicitly injected and measurements remain unknown.

| Existing control | Default physical output |
| --- | --- |
| Start | CH1, 500 ms pulse |
| Higher / lower accepted speed | CH2 / CH3, 500 ms pulse |
| Higher / lower accepted incline | CH4 / CH5, 500 ms pulse |
| Stop, workout complete, session fault or command timeout | CH6, 500 ms pulse; other outputs off |

This is a provisional button assignment, not a mapping of the old machine wires. CH7–8 are spare. Start and Stop are momentary; no relay remains held while running. An accepted Stop also pulses CH6 when already idle. Repeated idle updates do not extend the pulse. A new Start waits until an active Stop pulse finishes. Boot does not issue a Start or Stop pulse.

A direction change first requests both direction contacts off, with a nominal 50 ms gap before the new pulse. Unchanged targets and heartbeats do not extend pulses. Each accepted button command starts one pulse; additional presses while an output is busy are rejected rather than queued or coalesced. Pulse length does not encode a calibrated speed or angle. Each changed numeric target generates a direction pulse; a larger change does not imply extra pulses or measured movement. Workouts use this same mapping. Outputs follow requested values or raw button commands, not the generic layer's simulated ramp.

Set channel assignments and pulse length in **menuconfig → Nordic relay control mapping (unloaded bench only)**. Channels range from 1–8; 0 disables that function. Duplicate assigned channels are rejected at initialization. See `firmware/relay-bench.defaults` for the editable initial mapping; defaults apply to fresh sdkconfig files, while existing builds must be changed using menuconfig. Default builds leave this mode disabled. The previous `NORDIC_RELAY_RUN` option is replaced by `NORDIC_RELAY_START`; existing custom Run assignments must be migrated explicitly. Status JSON now reports `start` and `stop` channels instead of `run`.

The board binding was verified against the [manufacturer documentation](https://docs.waveshare.net/ESP32-S3-ETH-8DI-8RO/) and [official demo](https://files.waveshare.net/wiki/ESP32-S3-POE-ETH-8DI-8RO/ESP32-S3-POE-ETH-8DI-8RO-Demo.zip), `MAIN_WIFI_AP/I2C_Driver.h`, `WS_TCA9554PWR.h`, and `WS_Relay.cpp`: SDA42, SCL41, TCA9554 address 0x20, active-high CH1–8 on bits 0–7.

An independent relay task owns I2C, checks configuration and latch on updates, and uses a 500 ms controller-publication watchdog. The existing three-second browser command timeout clears running demand and the mapping requests a Stop pulse. Relay hardware faults and the publication watchdog retain the inactive-only recovery path; they cannot guarantee a Stop pulse. A relay fault latches, faults the shared service, and retries inactive initialization only; restart is required. Stop acknowledgements from the memory motion driver are not physical relay acknowledgements. Dashboard relay status separately reports the acknowledged output latch; contact position remains unmeasured. I2C failure, MCU reset, or power failure can defeat software shutdown, so keep all relay terminals disconnected for this mode.
