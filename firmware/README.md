# ESP-IDF firmware

Target: Waveshare ESP32-S3-ETH-8DI-8RO. Framework: **ESP-IDF v6.1**, pinned in CI. Language: C. No Arduino layer or Python application model.

## Build

Install the v6.1 ESP32-S3 tools using [Espressif's installation guide](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/get-started/index.html), then activate its environment with `export.sh` (Linux/macOS) or the ESP-IDF terminal (Windows). ESP-IDF itself uses Python build utilities.

From the repository root:

```sh
cd firmware
idf.py set-target esp32s3
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

Expect five tests and zero failures. This replaces the application image; reflash the main application afterward. CI compiles this test image but does not execute it on hardware. Host tests cannot verify electrical startup behavior, task timing, radio behavior, or mechanical stopping.
