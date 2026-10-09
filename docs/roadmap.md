# Development roadmap

## 0. Repository and simulator

- [x] Public repository foundation and hardware notes.
- [x] Hardware-independent state model and command-line demo.
- [x] Automated tests for state transitions, command validation, and lost communication.

## 1. Tablet interface and simulated transport

- [ ] Responsive local interface showing simulated status, speed, incline, and faults.
- [ ] Explicit simulation labeling and separate requested/actual values.
- [ ] Command protocol and single-operator session semantics.
- [ ] Exercise disconnect, stale command, and reconnect scenarios.

## 2. Unloaded board bring-up

- [ ] Confirm board model/revision and pin/expander mappings.
- [ ] Pin toolchain versions and add a reproducible firmware build.
- [ ] Default-off outputs and disabled hardware mode.
- [ ] Read one button and deliberately switch one unloaded relay.
- [ ] Wi-Fi control plus status feedback to an iPad.
- [ ] Power-cycle, reset, brownout, and link-loss validation.

## 3. Machine interface

- [ ] Collect motor, drive, lift, and sensor documentation.
- [ ] Review independent safety circuit and stopping/load-holding strategy.
- [ ] Select electrical interface, protection, wiring, and physical limits.
- [ ] Implement drive/lift adapters with interlocks and measured feedback.
- [ ] Conduct controlled machine commissioning before person-on-machine tests.

## 4. Training features

- [ ] Workout steps and smooth speed/incline transitions.
- [ ] Session logging and measured distance/elevation calculations.
- [ ] BLE setup or native-app transport.
- [ ] Owner selects a license before inviting reuse/contributions.
