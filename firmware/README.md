# ESP-IDF firmware

Target: Waveshare ESP32-S3-ETH-8DI-8RO. Framework: **ESP-IDF v6.1**, pinned in CI. Language: C. No Arduino layer or Python application model.

## Build

Install the v6.1 ESP32-S3 tools using [Espressif's installation guide](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/get-started/index.html), then activate its environment with `export.sh` (Linux/macOS) or the ESP-IDF terminal (Windows). ESP-IDF itself uses Python build utilities.

From the repository root:

```sh
cd firmware
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
```

The defaults select 16 MB flash and the ESP32-S3 USB Serial/JTAG console. Verify the delivered board's module and USB port before flashing. PSRAM is not required or enabled yet.

After recording/backing up factory firmware, with unloaded outputs and machine wiring disconnected:

```sh
idf.py -p YOUR_SERIAL_PORT flash monitor
```

Expected application log: `Boot complete: UNCONFIGURED; motion commands disabled`. No relay driver is loaded, so do not interpret this as a physical outputs-off guarantee. Exit the monitor with Ctrl+].

## Tests of the firmware code

Desktop tests compile the same `components/control/control.c` used by the ESP32 application. They do not emulate electronics or duplicate the controller in another language. With a C compiler installed, run from the repository root:

```sh
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -Ifirmware/components/control/include \
  firmware/components/control/control.c tests/control_tests.c -lm -o /tmp/control-tests
/tmp/control-tests
```

For the same tests on the actual ESP32 using ESP-IDF's bundled Unity framework:

```sh
cd firmware/test_app
idf.py set-target esp32s3
idf.py build
idf.py -p YOUR_SERIAL_PORT flash monitor
```

Expect twelve tests and zero failures. This replaces the application image; reflash the main application afterward. CI compiles this test image but does not execute it on hardware. Host tests cannot verify electrical startup behavior, task timing, radio behavior, or mechanical stopping.

## Relay driver

The TCA9554 driver and ESP-IDF I2C adapter are built but not connected to the application. See [driver behavior and integration limits](../docs/relay-driver.md). Desktop relay tests compile the real driver with register-access test callbacks:

```sh
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -Ifirmware/components/relay/include \
  firmware/components/relay/relay.c tests/relay_tests.c -o /tmp/relay-tests
/tmp/relay-tests
```

## Tablet connection

In `idf.py menuconfig`, open **Nordic Wi-Fi status page** and set a local password of 8–63 printable ASCII characters. An empty or invalid password disables Wi-Fi. The default SSID is `Nordic-Ski-Setup`. See [tablet instructions](../docs/tablet-connection.md). Credentials are compiled into the image: do not publish a configured binary or sdkconfig. CI builds with an empty password.

## Hosyond ESP32S USB test board

Detected chip: ESP32-D0WDQ6-V3 revision 3.1, 4 MB flash, 40 MHz crystal. Use `idf.py set-target esp32`, not `esp32s3`. Target-specific defaults select UART console, 4 MB flash, DIO and 40 MHz flash clock. The Waveshare target retains its own 16 MB and USB Serial/JTAG settings. No relay GPIOs or board I/O are initialized on either target.

Before flashing, save the current 4 MB flash to a private location. Local backups, credentials and builds go under ignored `.local/`; never upload them as public build artifacts. Set the Wi-Fi password locally as described above. CI builds both targets without a password.

## Software-only control test

Enable **Nordic Wi-Fi status page → Enable software-only control test** for the Hosyond bench build. It defaults off. This exercises the existing C controller with test limits and never initializes GPIO/relay outputs. See [test workflow and timeout behavior](../docs/software-test.md).

## Editing workouts

See [workout definitions](workouts/README.md). Run `node tools/generate-workouts.cjs` from the repository root after adding, editing, or removing JSON files. Commit the generated header and manifest with the definitions. Firmware builds reject stale definitions; no Node runtime is needed on the ESP32 or inside the ESP-IDF build container.
