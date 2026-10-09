# Firmware bring-up

Target: Waveshare ESP32-S3-ETH-8DI-8RO with isolated RS-485.

Firmware is intentionally not flashed or implemented yet. The next hardware step is to verify the delivered revision, manufacturer demo, relay expander, digital input mapping, and output polarity. No generic GPIO pin map is assumed.

Planned modules:

- Application state and validated commands.
- Wi-Fi API and status transport.
- BLE transport (subsequent milestone).
- Board adapter for inputs and relay outputs.
- Drive/lift adapter chosen after machine documentation arrives.

The first build must initialize all relay outputs inactive and keep physical output operation behind an explicit bench-test mode. Use no motor connections for bring-up. Simulator behavior is a reference for application logic, not verified firmware or a safety system.
