# Development roadmap

## Firmware foundation

- [x] Public repository and hardware notes.
- [x] Owner selected ESP-IDF.
- [x] ESP32-S3 application and shared C control component.
- [x] Native control tests and ESP-IDF Unity test image.
- [x] CI definition pinned to ESP-IDF 6.1.

## Board bring-up

- [ ] Verify delivered model/revision, USB connection, and manufacturer mapping.
- [ ] Record/back up factory firmware; flash application and execute Unity tests.
- [x] Implement TCA9554 driver and ESP-IDF adapter with register-level tests.
- [ ] Implement board adapter and verify inactive outputs during boot/reset/power loss.
- [ ] Read physical input and deliberately switch an unloaded relay.
- [ ] Select Wi-Fi/BLE transport and tablet technology with owner approval.
- [ ] Implement authenticated commands/status and test link loss/reconnection.

## Machine integration

- [ ] Collect roller drive, lift, and sensor documentation.
- [ ] Establish independent safety circuit, stopping and load-holding strategy.
- [ ] Approve electrical interface, physical limits, timeout and ramp behavior.
- [ ] Implement motion adapters, interlocks, and measured feedback.
- [ ] Complete controlled machine commissioning before person-on-machine tests.

## Training features

- [ ] Workout steps, session logging, measured distance/elevation.
- [ ] Owner selects license before inviting reuse/contributions.
