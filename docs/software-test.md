# Software-only tablet control test

Enable `CONFIG_NORDIC_SOFTWARE_TEST` in a local build to exercise controller logic without any physical outputs. It defaults off. The Hosyond bench build enables it; the default Waveshare build does not. The real application controller remains unconfigured. A separate test session uses the same C control component with artificial limits of 0–5 m/s and 0–10% grade and a three-second timeout. None of these values configure a machine.

## Tablet workflow

1. Join the controller's network and open or refresh http://192.168.4.1/.
2. Confirm the software-test / physical-outputs-disabled notice.
3. Select **Enable controls**, then **Start**. Start always begins at zero requests.
4. Enter speed and incline and select **Apply settings**. The status card shows the requests acknowledged by the controller, not measured speed or angle.
5. Select **Stop** to clear requests and return to idle.
6. To test connection loss, start a test, set nonzero requests, then disconnect tablet Wi-Fi. After three seconds without commands the ESP32 clears requests, latches a timeout fault, and expires ownership. Reconnect, take control, reset the fault, then deliberately start again. No previous targets are restored.

The page sends a heartbeat approximately every half second while it is visible and owns control. Backgrounding attempts a stop and stops heartbeats; backgrounding, closing, or losing connectivity may instead produce a timeout fault. Do not infer controller state from an unavailable page. Reloading loses the page's token and requires taking control again after the previous lease expires.

## Implementation and scope

One page at a time owns the test session. Claim creates a random 64-bit token after Wi-Fi has started. Authenticated commands use increasing sequence numbers; duplicate/older numbers are rejected. A new owner cannot clear a latched fault by reconnecting. Rejected commands do not renew the lease. Status never exposes the token.

The HTTP handlers and independent 20 ms main loop serialize short, in-memory test-state operations with a lock. No networking or I/O runs under the lock. The main loop enforces deadlines even when a request stalls. The test state has no relay/GPIO adapter. The production control component remains owned by the main task.

The existing HTTP interface adds `POST /api/test/claim` and `POST /api/test/command`. Both require the custom `X-Nordic-Test: 1` header; no CORS/preflight support is provided. Command bodies are bounded to 127 bytes and contain token, sequence, action, speed, and grade. Access to the WPA2 network is the authentication boundary for this bench-only interface. This is not a commissioned machine-control or production authorization protocol.

C tests cover disabled mode, ownership, replay, invalid setpoints, heartbeat renewal, stop, deadline boundaries, lease loss, and deliberate reset/restart. Actual tablet behavior must also be checked against the flashed board.

The tablet dashboard supports both km/h and mph. Switching units keeps the speed request unchanged; the secondary reading shows the other unit. Sliders and +/− controls edit pending settings, while Apply settings explicitly sends them to the controller. Accepted readings are separate from pending values. The fixed bottom action bar keeps Start, Apply settings, and Stop available while scrolling.
