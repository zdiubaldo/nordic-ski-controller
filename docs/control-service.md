# Shared control contract — version 1

Step 1 implements a common service for the existing HTTP webpage, future BLE apps, and a future wired touchscreen. HTTP and the USB diagnostic adapter are connected today. No BLE or wired protocol, mobile framework, or screen hardware is selected by this implementation.

## Authority and control policy

The ESP-IDF `controller_runtime` task owns a single C `control_service`. Adapters authenticate clients, assign a transport/peer identity, and submit commands; they never write relays directly. Status is read from a copied snapshot.

`SERVICE_SINGLE_OWNER` is the current policy. Up to eight authorized client sessions can coexist. Registration is separate from acquiring control. A registered non-owner can read state, send heartbeats, disconnect, and Stop. Start, adjustments, fault reset, and workouts require ownership. Releasing ownership stops the session and allows another client to claim it. Disconnecting a viewer does not stop the owner. No interface can take over an occupied owner slot.

`control_service_set_policy` is the extension point for future shared control. `SERVICE_SHARED_RESERVED` is intentionally rejected: conflict resolution and takeover are not implemented or implicitly enabled. Applications use the same commands regardless of a future ownership-policy implementation.

Transport identities are HTTP=1, BLE=2, wired=3, USB=4. HTTP uses adapter peer 1 plus separate random 64-bit bearer tokens for each page. The controller assigns non-secret client IDs for status. Future BLE/wired adapters must authenticate their links and register their own identities; a token from another transport/peer cannot be used there. Bearer tokens are never included in public status. Current HTTP authorization is the existing WPA2 bench-network boundary and same-origin custom request header; this is not an Internet-facing authentication system.

## Commands

| Command | Permission and meaning |
| --- | --- |
| Register | Adapter establishes an authorized session; does not acquire control. |
| Claim | Acquire the vacant owner slot. |
| Release | Owner stops and releases control; viewer remains registered. |
| Start | Owner requests Start from idle. |
| Stop | Any registered client can request Stop, including while idle or faulted. Cancels pending movement and workouts. |
| Speed up/down, incline up/down | Owner requests one momentary button press while running. |
| Heartbeat | Refresh this client's session; only the owner refreshes the running command watchdog. |
| Reset | Owner clears a recoverable fault without starting. |
| Targets | Owner sets requested speed (m/s) and incline (% grade), including while running a workout. |
| Workout | Owner starts an ESP-managed preset with duration and intensity. |
| Cancel workout | Owner stops/cancels the workout. |
| Disconnect | End this client session; owner disconnect also stops and faults. |

All transports support numeric targets and workouts through the shared service, including physical relay builds. Capabilities advertise `numeric_targets=true`, `workouts=true`, and `calibrated_targets=false`; `button_controls` additionally indicates raw button-press support. Requested values are synchronized across clients, while actual measurements remain `null`.

The restored bench mapping generates one pulse in the corresponding direction when an accepted requested value changes. A larger change does not generate a calibrated number of pulses. Unchanged values do not repeat a pulse. Workouts use the same target path, with manual overrides lasting until the next segment. The ESP owns workout timing and completion. Display unit conversion belongs to clients; protocol speed is always m/s. Numeric commands, raw button commands, and workouts share ownership, sequence, epoch, and Stop protections. Raw button presses remain available for diagnostics and do not invent new numeric settings.

For raw button commands, one press at a time is accepted. While Start, Stop, or a directional pulse is active, another movement press returns `output_busy`; it is not silently coalesced or saved to execute later. Each accepted button press has an event counter. Directional outputs keep the existing 50 ms release gap and configured pulse length. Stop cancels active directions. CH1 Start, CH2/3 speed, CH4/5 incline, CH6 Stop remain the provisional mapping.

## Ordering, results, and Stop

All post-registration commands carry a nonzero, increasing per-client 32-bit sequence. Both successful and semantically rejected commands consume their sequence; duplicates return `replayed_sequence` and cannot repeat a pulse. Tokens are connection-scoped, not persisted by the page; reconnecting requires a new registration and explicit Claim/Start.

Commands that could change operation carry the last observed `control_epoch`. Stop, owner release/loss, and a running-to-stopped transition increment that epoch. Requests prepared before that event return `stale_epoch`, including a queued Start that might otherwise restart the machine. Stop and connection-management commands do not require a matching epoch. Clients must refresh status, not automatically retry movement with a newer epoch.

The runtime has a bounded normal queue and a separate Stop queue. Stop is selected before pending normal work; older queued movement then fails its epoch check. A command already executed before Stop arrived cannot be undone, but Stop cancels its remaining output. Full queues return `output_busy`. Each synchronous submission owns a separate completion semaphore and result, so replies cannot cross between clients. These APIs are task-only, never ISR APIs.

Results contain `accepted`, a stable reason string, client ID, sequence, epoch (top-level `control_epoch` on HTTP), and revision. Reasons include `not_owner`, `control_in_use`, `unauthorized`, `replayed_sequence`, `stale_epoch`, `unsupported_command`, `output_busy`, `invalid_command`, `wrong_state`, and `driver_fault`.

Acceptance acknowledges the request, not physical motion. The relay task separately publishes `relays.known`, `mask`, `ready`, `fault`, and `button_ack_event`. Matching `button_ack_event` to the accepted `button_event` confirms the expander latch acknowledged a directional press. It does not prove relay contacts or machine movement. Start/Stop output changes are observed through the existing latch mask; the memory driver's acknowledgement is not a physical contact acknowledgement.

## Status and connection behavior

Status includes protocol version, boot ID, snapshot revision, control epoch, policy, owner client ID/transport, number of registered clients, mode/fault, capabilities, last command result, workout progress, and explicitly unknown measurements. The HTTP response adds relay-driver state and channel assignments. Each command response contains its own result even if another adapter executes a later command before status is copied.

`revision` is a decimal string to preserve the 64-bit counter in JavaScript. It advances on samples and commands, not just user-visible changes. Clients discard older revisions from the same boot. A changed boot ID clears local ownership and queued actions. HTTP polling supplies status; no new subscription transport is implemented in step 1.

The existing three-second owner heartbeat/lease policy remains. Registered viewers heartbeat independently and cannot keep a missing owner alive. The page sends Stop on hiding only if it owns control, so hiding a viewer cannot stop someone else's session. The phone-lock/disconnection policy is still a step-2 design decision. No automatic reacquisition, resumption, or movement retry is introduced.

## HTTP encoding

All mutation requests retain `X-Nordic-Test: 1` and no CORS allowance.

- `POST /api/session`, empty body: register a viewer; returns `protocol_version`, token, client ID, and current epoch.
- `POST /api/test/claim`, empty body: register and claim in one operation for the bench test script.
- `POST /api/test/command`: `v1 TOKEN SEQUENCE EPOCH ACTION VALUE VALUE`.
- `GET /api/status`: shared snapshot plus relay status.
- `GET /api/workouts`: shared workout catalog.

Actions are `claim`, `release`, `start`, `stop`, `speed_up`, `speed_down`, `incline_up`, `incline_down`, `heartbeat`, `reset`, `cancel_workout`, `disconnect`, `targets`, or `w:WORKOUT_ID`. Ordinary actions require both values to be zero. Targets use m/s and percent grade; workouts use whole minutes and intensity percent. Versionless old command bodies are rejected; the webpage and bench script are updated together.

HTTP 200 acknowledges acceptance, 409 returns a structured rejection plus status, and 400 rejects malformed wire data before execution. `control_service_parse` is shared between the ESP HTTP adapter and local preview. The token/sequence/epoch/action fields are not authority to choose a transport identity.

## Verification and remaining work

`tests/contract_tests.c` exercises four logical transports, observer Stop, exclusive ownership, transport spoofing, replay protection, stale commands after Stop, release/handoff, viewer disconnection, lease expiry, button capabilities, version parsing, and status encoding. `tests/service_tests.c` exercises numeric settings, a complete workout, manual override, cancellation, and Stop epochs with physical relay capabilities for every transport identity. Relay mapping tests cover repeated discrete presses and cancellation. Browser tests exercise two pages, revision ordering, ownership display, reconnect behavior, and numeric preview regression.

Physical firmware is compiled but not flashed as part of step 1. BLE/wired concurrency in these checks uses service identities, not real radios or touchscreen hardware. Machine wiring, measured feedback, calibration, mobile apps, and a revised connection policy remain outside this step.
