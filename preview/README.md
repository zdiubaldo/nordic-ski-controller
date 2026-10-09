# Interactive local dashboard

This serves the real dashboard and runs the firmware's C control/session components as a native process. It does not connect to an ESP32, open serial ports, or control hardware. The page identifies itself as a local preview. Node's built-in HTTP server provides the same endpoints; no additional packages are required.

From the repository root, with a C compiler and Node installed:

```sh
node tools/generate-workouts.cjs
mkdir -p .local
cc -std=c11 -Wall -Wextra -Werror \
  -Ifirmware/components/control/include -Ifirmware/components/test_session/include \
  firmware/components/control/control.c firmware/components/test_session/test_session.c \
  preview/controller.c -o .local/preview-controller
node preview/server.cjs
```

Open http://127.0.0.1:8766/. Enable controls, Start, adjust speed/incline, and changes apply automatically while running. Stop clears requests. Closing the page stops heartbeats, so an active session faults after three seconds. Reopening does not restart it; enable controls, reset the fault, then Start.

The preview listens only on localhost. State lives in memory and resets when the server restarts. Keep only one controlling tab open. The C loop checks deadlines every 20 ms independently of page requests; the HTTP adapter is preview-only and does not emulate the ESP32 radio or electrical behavior.

## Preset workouts

Choose Steady, Intervals, or Rolling Hills before starting. Set 1–120 whole minutes and 50–150% intensity (100% is the base profile). The supplied presets have ten equal segments including a warm-up and cool-down; custom presets can use any 1–64 weighted segments. Intensity scales both speed and grade. The shared C session runner owns monotonic timing; browser heartbeats only maintain the existing three-second control lease. The final segment ends by clearing requests and returning to idle.

Manual speed/incline changes override the current segment only. The next segment restores the preset targets. Stop cancels the workout; there is no automatic resume after a stop, fault, reconnect, or restart. Switching away from the browser page requests Stop, and loss of heartbeats faults an active session.

These profiles are software-only examples, not machine commissioning values or training prescriptions. The JSON definitions live in `firmware/workouts/`; see [the workout authoring guide](../firmware/workouts/README.md). After editing, regenerate and recompile the native controller, then restart the preview. The webpage obtains names, descriptions, defaults, and segments from the running controller’s `/api/workouts` catalog. Physical outputs remain disabled. The real machine will also need approved speed/incline transition limits before these targets can drive hardware.
