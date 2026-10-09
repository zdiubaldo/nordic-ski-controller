# Software-only tablet control test

Enable `CONFIG_NORDIC_SOFTWARE_TEST` in a local build to exercise controller logic without any physical outputs. It defaults off. The Hosyond bench build enables it; the default Waveshare build does not. The real application controller remains unconfigured. A separate test session uses the same C control component with artificial limits of 0–5 m/s and 0–10% grade and a three-second timeout. None of these values configure a machine.

## Tablet workflow

1. Join the controller's network and open or refresh http://192.168.4.1/.
2. Confirm the software-test / physical-outputs-disabled notice.
3. Select **Enable controls**, then **Start**. Start always begins at zero requests.
4. Enter speed and incline while running; changes apply automatically. The status card shows the requests acknowledged by the controller, not measured speed or angle.
5. Select **Stop** to clear requests and return to idle.
6. To test connection loss, start a test, set nonzero requests, then disconnect tablet Wi-Fi. After three seconds without commands the ESP32 clears requests, latches a timeout fault, and expires ownership. Reconnect, take control, reset the fault, then deliberately start again. No previous targets are restored.

The page sends a heartbeat approximately every half second while it is visible and owns control. Backgrounding attempts a stop and stops heartbeats; backgrounding, closing, or losing connectivity may instead produce a timeout fault. Do not infer controller state from an unavailable page. Reloading loses the page's token and requires taking control again after the previous lease expires.

## Implementation and scope

One page at a time owns the test session. Claim creates a random 64-bit token after Wi-Fi has started. Authenticated commands use increasing sequence numbers; duplicate/older numbers are rejected. A new owner cannot clear a latched fault by reconnecting. Rejected commands do not renew the lease. Status never exposes the token.

The HTTP handlers and independent 20 ms main loop serialize short, in-memory test-state operations with a lock. No networking or I/O runs under the lock. The main loop enforces deadlines even when a request stalls. The test state has no relay/GPIO adapter. The production control component remains owned by the main task.

The existing HTTP interface adds `POST /api/test/claim` and `POST /api/test/command`. Both require the custom `X-Nordic-Test: 1` header; no CORS/preflight support is provided. Command bodies are bounded to 127 bytes and contain token, sequence, action, speed, and grade. Access to the WPA2 network is the authentication boundary for this bench-only interface. This is not a commissioned machine-control or production authorization protocol.

C tests cover disabled mode, ownership, replay, invalid setpoints, heartbeat renewal, stop, deadline boundaries, lease loss, and deliberate reset/restart. Actual tablet behavior must also be checked against the flashed board.

The tablet dashboard supports both km/h and mph. Switching units keeps the speed request unchanged; the secondary reading shows the other unit. Sliders and +/− controls send changes automatically while running, coalescing rapid adjustments for 180 ms. Stop takes priority over queued adjustments. Accepted readings are separate from pending values. The fixed bottom action button shows Start while idle and changes to a red Stop only when the controller confirms it is running. It is disabled when disconnected or faulted; fault reset remains separate.

## Preset sessions

The existing authenticated command endpoint also accepts `w:<id>` (for example `w:hills`), where the ID must exist in the compiled JSON catalog. These actions start a workout from idle; the two numeric fields specify whole duration minutes (1–120) and intensity percent (50–150). All commands retain the existing owner token and increasing sequence requirements. Invalid settings and attempts to start while running are rejected.

The shared C runner chooses a segment using its relative duration weight using elapsed monotonic time. `targets` overrides only the current segment; a segment transition restores the preset. Automatic transitions do not renew the command timeout. Stop, faults, and expired ownership cancel the workout. Finishing the last segment returns to idle with zero requested speed and grade. A new Start always starts from the beginning.

Status includes a `workout` object with stable string preset ID (`manual` for manual mode), segment count, active/complete flags, zero-based segment, elapsed and duration milliseconds, segment time remaining, intensity multiplier, next speed/grade, and manual override flag. The UI displays speed in the selected unit while the protocol retains m/s.

## Browser recovery and timing

Foreground commands queue behind an in-flight heartbeat; Stop clears queued target changes and takes priority. Inputs are unavailable during Start/Stop/Reset so their replies cannot erase changes entered during those actions. Status requests started before a newer command are discarded when they return. A fault replaces workout-running messages, and non-owning viewers synchronize their disabled input fields with accepted controller requests. Refresh intentionally does not retain ownership or automatically resume a session.

The browser interpolates progress from the last confirmed controller elapsed time using its monotonic clock. It advances for at most 1.5 seconds without a fresh report and never advances beyond a confirmed segment boundary or declares completion itself. Device timing and segment changes remain authoritative. HTTP timeouts include response bodies.
