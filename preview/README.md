# Interactive local dashboard

This serves the real dashboard and runs the firmware's C control/session components as a native process. It does not connect to an ESP32, open serial ports, or control hardware. The page identifies itself as a local preview. Node's built-in HTTP server provides the same endpoints; no additional packages are required.

From the repository root, with a C compiler and Node installed:

```sh
mkdir -p .local
cc -std=c11 -Wall -Wextra -Werror \
  -Ifirmware/components/control/include -Ifirmware/components/test_session/include \
  firmware/components/control/control.c firmware/components/test_session/test_session.c \
  preview/controller.c -o .local/preview-controller
node preview/server.cjs
```

Open http://127.0.0.1:8766/. Enable controls, Start, adjust speed/incline, and Apply settings. Stop clears requests. Closing the page stops heartbeats, so an active session faults after three seconds. Reopening does not restart it; enable controls, reset the fault, then Start.

The preview listens only on localhost. State lives in memory and resets when the server restarts. Keep only one controlling tab open. The C loop checks deadlines every 20 ms independently of page requests; the HTTP adapter is preview-only and does not emulate the ESP32 radio or electrical behavior.
