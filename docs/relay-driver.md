# Relay expander driver

The board's [Waveshare documentation](https://www.waveshare.com/wiki/ESP32-S3-ETH-8DI-8RO) identifies a TCA9554 expander at address 0x20. Register behavior is based on the [TI TCA9554 datasheet, section 8.6](https://www.ti.com/lit/ds/symlink/tca9554.pdf).

`firmware/components/relay` contains a C driver and an ESP-IDF I2C adapter. Neither is called by the main application. No board pins, channel assignments, or output polarity are assumed. The future board adapter must create the I2C device and supply verified inactive levels.

Initialization writes the inactive output latch and verifies it before configuring the eight pins as outputs. It then verifies configuration. Each subsequent output update checks configuration and reads back the output latch. Any transaction error or mismatch invalidates the driver; another output request is rejected until explicit reinitialization. Reinitialization requests all relays inactive and never restores the old command.

`commanded_mask` is the last successfully acknowledged logical command, not measured relay contact state. When `ready` is false, hardware state is unknown. A failed transaction may have reached the device. Register readback cannot prove contact operation or guarantee shutdown after bus failure. This driver does not provide the machine's independent stopping function. Expander reset detection is performed during writes; periodic health monitoring remains to be integrated.

The ESP-IDF adapter bounds each I2C transaction to 50 ms. Multiple transactions can take longer than the 20 ms control tick, so do not call it synchronously from that loop without completing the task/timing design. No machine stopping deadline has been selected.

Tests substitute only the register-access functions, while executing the actual driver C code. They exercise startup order, eight output bits, both polarities, failures at every initialization/update transaction, readback mismatches, missing callbacks, and expander configuration loss. The same tests are compiled into the onboard Unity image. This is not a hardware emulator.

Before connecting the driver: verify schematic/revision, I2C pins, address, relay polarity and mapping; test power-on and MCU-only reset with unloaded contacts; measure timing; add runtime health supervision and fault propagation to control; then design machine-level relay assignments and interlocks from drive/lift documentation.
