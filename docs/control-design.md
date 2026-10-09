# Control design

## Approved foundation

ESP-IDF was selected by the owner. Firmware is C with an ESP-IDF component shared by the application and tests. No hardware emulator has been selected. The owner selected Wi-Fi with a browser interface and explicitly approved the controller creating its own network. The initial ESP-IDF HTTP interface is read-only. BLE and the authenticated motion-command protocol remain pending.

## Current component

- Startup is idle with zero requested motion and no approved configuration.
- Start requires commissioned finite limits, a positive command timeout, and healthy interlock feedback.
- Valid commands update speed (m/s) and incline (percent grade) together and renew the command deadline.
- Nonfinite, negative, and out-of-range commands are rejected without changing either target or extending the deadline.
- The periodic tick runs independently of command reception. Expiry at the deadline faults before a late command can renew it.
- Interlock loss or a backward timestamp latches a fault and clears requested motion.
- Stop clears demand but never clears a fault. Reset requires healthy interlock feedback and leaves the controller idle. Starting again never restores previous targets.

A single task must own the component; future transports must queue commands to that task. Use monotonic microseconds from `esp_timer_get_time()`. Tests use deliberately artificial limits/timeouts, not machine configuration. No transport currently calls start or supplies commissioning configuration.

Clearing a requested speed/grade is not a physical stop or lift lowering command. The future hardware adapter must define controlled stop and load holding independently of the numeric requests. Actual motion feedback is not implemented.

## Required before operation

Verify output polarity, expander mapping, power-up/reset states, physical fault causes, direction interlocks, travel limits, drive feedback, stopping behavior, and independent safety circuits. The main application currently supplies unhealthy interlock feedback and makes no board I/O writes.

Wireless command handling must include operator authorization, single-controller ownership, bounded messages, stale/replayed command rejection, and explicit restart after faults. A wireless stop is not the independent emergency stop. Determine drive-level timeout behavior as part of machine integration.
