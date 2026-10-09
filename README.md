# Nordic Ski Controller

An early-stage controller project for a custom Nordic ski training machine with a carpeted roller and adjustable incline.

Target controller: **Waveshare ESP32-S3-ETH-8DI-8RO**, standard Ethernet / isolated RS-485 version, with Wi-Fi and Bluetooth Low Energy (BLE).

## Current status

The hardware has been ordered. Motor, drive, lift, and sensor specifications are not yet known. This repository currently contains a **software-only simulator**, tests, and an implementation plan. It does not control a physical machine, implement ESP32 firmware, or provide a tablet interface yet.

## Try the simulator

Requires Python 3.10 or newer; no third-party packages are needed.

```sh
python3 -m simulator
python3 -m unittest discover -s tests -v
```

The demo runs a simulated workout, then shows the controller entering a fault when communication expires. Speed and incline use arbitrary **0–100 simulation units**, not real machine limits, km/h, degrees, or percent grade.

## Planned system

```text
iPad / phone browser -- local Wi-Fi --> ESP32 application controller
Native app / setup   -- BLE ---------> ESP32 application controller
                                       | wired commands and feedback
                                       +--> roller drive
                                       +--> lift controller

Independent hardwired safety circuit ------> drive / lift safety functions
```

The controller will manage operating state locally. Wireless commands are requests, and an app stop button is not the machine's emergency stop. BLE support is planned separately from the browser interface; iPad browser Bluetooth is not assumed.

## First hardware milestone

1. Verify the delivered board model, revision, and manufacturer pin assignments.
2. Back up or record the factory firmware and validate the board with unloaded outputs.
3. Implement firmware that starts with all outputs off.
4. Connect an iPad interface over local Wi-Fi.
5. Deliberately enable an unloaded relay and display a physical button input.
6. Verify output behavior during disconnection, reset, brownout, and reconnection.

See [hardware notes](docs/hardware.md), [control design](docs/control-design.md), and [roadmap](docs/roadmap.md).

## Repository layout

| Path | Purpose |
| --- | --- |
| `simulator/` | Hardware-independent reference model and demonstration |
| `tests/` | State transition, timeout, and input validation tests |
| `firmware/` | Hardware bring-up requirements; firmware implementation pending |
| `docs/` | Design decisions, unknowns, and milestones |
| `.github/workflows/` | Automated simulator checks |

## Before connecting the machine

This is prototype application software, not a safety controller. Relay contact ratings alone do not establish suitability for a motor. An independent emergency-stop and load-holding design must account for roller stopping distance and lift behavior. Have the machine power and safety circuits designed or reviewed by a qualified machine-controls professional before powered testing with a person on the equipment.

Do not commit Wi-Fi passwords, tokens, private network settings, or unreviewed equipment photos containing personal information. Keep local configuration outside version control. Public visibility does not imply a selected open-source license; licensing is still to be decided by the owner.
