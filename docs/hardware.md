# Hardware notes

## Selected controller

- Waveshare **ESP32-S3-ETH-8DI-8RO** (RS-485 version; no `-C` suffix).
- Eight relay outputs and eight digital inputs.
- Wi-Fi, BLE, Ethernet, and isolated RS-485.
- Manufacturer documents USB-C 5 V / 1 A or 7–36 V DC terminal power.
- Package includes antenna, rail-mount case, and screwdriver.

Sources: [manufacturer product page](https://www.waveshare.com/esp32-s3-eth-8di-8ro.htm) and [manufacturer documentation](https://www.waveshare.com/wiki/ESP32-S3-ETH-8DI-8RO).

Verify the physical board revision against the documentation before assigning pins. Relay control may use an I/O expander; do not infer direct GPIO mappings from other ESP32 relay boards. Do not assume all digital inputs support high-rate encoder signals.

## Bench equipment

- USB data cable and computer for programming.
- USB-C supply for standalone board tests.
- Multimeter, wire stripper, hookup wire, and suitable terminals.
- Momentary buttons and lever switches for input simulation.
- Optional enclosed DC supply and fused low-voltage test wiring for external loads.

Start with unloaded relay contacts. No motor power is involved in the first milestone. Before simultaneous USB and terminal power, check the exact board's power-path documentation. Only the DC power terminals accept the documented higher input voltage; USB-C does not.

## Information needed before selecting the power interface

| Item | Details needed | Status |
| --- | --- | --- |
| Roller motor | Manufacturer, model, AC/DC, voltage, current, power, phase | Unknown |
| Existing drive | Model, manual, control terminals, command protocol, fault outputs | Unknown |
| Speed reference | Modbus, 0–10 V, preset contacts, or another interface | Unknown |
| Lift | Motor/actuator/hydraulic type, controller, voltage, current, holding behavior | Unknown |
| Limits | Upper/lower switches, contact wiring, independent overtravel protection | Unknown |
| Feedback | Actual roller speed and lift position/angle sensors | Unknown |
| Safety | Emergency stop, pull cord, stopping method, braking, load holding | Unknown |
| Operating envelope | Speed, incline, acceleration, and stopping limits | Unknown |

No relay channel assignments, motor switching ratings, or real operating limits are approved yet. Use a motor drive appropriate to the actual motor. Relays can issue discrete control commands; they do not provide continuously variable motor speed by themselves.
