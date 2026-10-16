# Tests against the ESP32 webpage

These Playwright tests run in installed Google Chrome against the real device. They require software-only firmware with physical outputs disabled. They do not flash firmware or change network settings.

Dependencies must be installed while online (`pnpm install --frozen-lockfile`, with Node and Google Chrome installed).

1. Close controller pages on your tablet, phone, and other browser tabs so they do not hold the control session.
2. Manually connect this computer to **Nordic-Ski-Setup**.
3. From the repository root, run:

   ```sh
   ./tools/run-device-tests.sh
   ```

   For just the reported refresh issue:

   ```sh
   ./tools/run-device-tests.sh --grep 'refresh during workout'
   ```

4. Wait for the test summary in the terminal, then manually reconnect to your normal network.

The runner needs no internet after dependencies are installed. It never reads Wi-Fi passwords or changes connections. Override `NORDIC_BASE_URL` if the device has another address.

Results are saved locally in `.local/device-test-run.log`, `.local/device-report/`, and `.local/device-results/`. Failed tests retain screenshots and traces. These files may contain temporary controller-session tokens and are ignored by Git. Runs overwrite the previous report; preserve it before another run if needed.

Coverage includes refresh, reconnect, multiple browser sessions, delayed/lost responses, manual controls, unit conversions, stop priority, workout transitions/completion, catalog rendering, input validation, protocol rejection, and responsive layouts. Chromium tests do not replace an actual iPad/Safari check or physical-machine validation. The full 19-case suite passed against the local C-controller preview after the UI fixes. The earlier ESP32 run passed 14 of 18 cases; the corrected firmware still needs an on-device regression run. Local success does not establish device success.

For regression development without changing Wi-Fi, explicitly opt into the local C-controller preview:

```sh
NORDIC_BASE_URL=http://127.0.0.1:8766 NORDIC_ALLOW_PREVIEW=1 NORDIC_REPORT_DIR=.local/preview-report ./tools/run-device-tests.sh --output .local/preview-results
```

This is a local regression run, not proof of ESP32 behavior. Normal device runs still reject the preview. The countdown regression delays real status replies and checks intermediate UI progress; it does not alter the device clock.

## Long checks are opt-in

The default browser run skips the real one-minute workout-completion check. Completion logic is covered by C tests that advance timestamps immediately, without sleeping. Only when explicitly wanted, run the full-duration device check with:

```sh
NORDIC_LONG_TESTS=1 ./tools/run-device-tests.sh --grep 'workout completes'
```

Other browser tests still exercise real short connection timeouts and segment transitions.
