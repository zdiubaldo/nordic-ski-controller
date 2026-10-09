# Tablet connection

Approved approach: the ESP32 creates its own password-protected Wi-Fi network and serves the interface directly to the tablet browser. No router, internet connection, installed tablet app, or cloud service is required.

## Once the board arrives

1. Set the access-point password locally in ESP-IDF menuconfig under **Nordic Wi-Fi status page**, build, and flash. There is no shared default password. Empty or invalid credentials leave Wi-Fi disabled.
2. On the iPad, open Settings → Wi-Fi and join **Nordic-Ski-Setup** using that password.
3. Stay connected even though this network provides no internet service.
4. Open Safari and enter `http://192.168.4.1/`. The device also logs its actual address over USB.
5. The page reports connection state, controller state, and uptime. It marks status unavailable after a failed request or stale control-task update.

The tablet leaves its usual Wi-Fi network while connected. There is no captive portal or automatic browser launch. The firmware allows up to two Wi-Fi clients for status viewing; neither can issue motion commands.

## Implemented behavior

The ESP-IDF access point uses WPA2-Personal. The embedded page uses HTML, CSS, and browser JavaScript with no external downloads. `GET /api/status` returns a copied controller status; the HTTP task cannot modify the controller. The page polls once per second after each request finishes, with a 2.5-second request timeout. Control updates older than one second are treated as unavailable.

Only `GET /` and `GET /api/status` are registered. The unavailable Start button has no command handler. Speed/incline commands, operator authorization, and session ownership must be implemented and reviewed before enabling motion.

This is HTTP on the password-protected local network, not HTTPS. Anyone with network access can view status. Passwords are supplied at build time and embedded in firmware; ignored `sdkconfig` files and configured binaries must remain private. Network startup runs once per boot; configuration or startup failures are logged without enabling motion. There is no automatic NVS erase on initialization failure.

## Verification limits

CI compiles the ESP32-S3 application and onboard test image and runs the control/relay C tests. Radio startup, Safari connectivity, reconnection, and physical device behavior require the board. The browser page does not represent verified machine operation.

Framework references: [ESP-IDF HTTP server](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/protocols/esp_http_server.html) and [Espressif access-point example](https://github.com/espressif/esp-idf/tree/v6.1/examples/wifi/getting_started/softAP).
