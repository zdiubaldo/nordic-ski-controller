# BLE client protocol and board validation

The opt-in ESP-IDF 6.1 NimBLE adapter connects two BLE clients to the existing
shared controller runtime. Wi-Fi HTTP, BLE and USB remain enabled in parallel.
An [Android-first mobile connection prototype](mobile-trial.md) is available on the trial branch; physical phone validation is pending. The webpage and shared control policy are unchanged:
one owner controls settings/workouts; any registered authorized client may Stop.
A BLE bond authorizes a connection, not ownership or motion.

## Build configuration

Combine `firmware/ble.defaults` with the existing local board and Wi-Fi defaults.
The profile selects ESP-IDF’s 1500 KiB single-app partition (the original 1 MiB
partition is too small for BLE plus the webpage), preserving NVS/PHY locations
and application start address. It enables NimBLE, two connections, four remembered peers, persistent
NVS bonds, Secure Connections, and Wi-Fi/Bluetooth software coexistence. Legacy
pairing is disabled. `CONFIG_NORDIC_BLE` defaults off; disabled builds have a small
stub and retain their current behavior. Local credentials and binaries must stay
under ignored `.local/` paths. Preserve NVS when flashing; do not erase flash to
update this feature. On ESP32 boards without USB Serial/JTAG, this build can compile
but the current USB-assisted enrollment path is unavailable; production enrollment
on those boards is not implemented. The target Waveshare ESP32-S3 supports it.

The normal software-test/relay configuration remains necessary for actuating the
existing bench controls. BLE does not enable outputs or bypass the runtime.

## Enrollment and removal

### Temporary PIN-free bench mode

`CONFIG_NORDIC_BLE_NO_PIN_TEST=y` selects Secure Connections Just Works pairing
for unloaded bench testing. It requires `CONFIG_NORDIC_SOFTWARE_TEST=y` and is
disabled by default. Android may show a Pair confirmation, but no PIN or USB
pairing window is required. Encryption and bonding are still required; this mode
does not authenticate the peer with a passkey, so nearby devices can enroll.
Control ownership, Stop priority, and communication timeouts remain in effect.
Rebuild with this option disabled to restore passkey authentication. PIN-free
bonds may need to be forgotten on both ends before pairing with the normal image.

### Normal PIN-protected enrollment

While the controller is **idle**, send `ID ble-pair` over the existing USB console.
The JSON reply contains a six-digit `passkey` string (including leading zeros) and
`pairing_seconds:120`. Enter it on the connecting central. The code is temporary
and returned only through physical USB; it is not put in normal BLE advertisements,
status, or logs. Only authenticated encrypted bonded connections can use commands
or receive status. Outside the window, new pairing is rejected. Previously bonded
phones reconnect without opening another window. Existing bonds are not silently
replaced by repeat-pairing requests.

`ID ble-forget` queues removal of all BLE bonds and disconnection of BLE clients.
It requires idle state, closes the pairing window, and responds `forget_queued`;
it is not a synchronous proof that flash storage has been updated. Re-pair each
phone afterward. This deliberately does not erase Wi-Fi configuration or other NVS
namespaces. A faulted or running controller rejects both administrative operations.

## GATT service

Advertised name: `Nordic Ski`. Service UUID:
`75e60000-6b92-4e3d-a347-8bc06c51d210`. Identify by service UUID, not name alone.
Characteristic UUIDs share that base and replace `0000` with the suffix below.

| Suffix | Characteristic | Access |
| --- | --- | --- |
| `0001` | Device info | Read JSON: version, boot ID, stable device ID, capabilities and limits |
| `0002` | Commands | Encrypted authenticated Write with response |
| `0003` | Results | Notify; subscribe before writing commands |
| `0004` | Status | Notify; full shared snapshot plus relay enabled/ready/known/fault/mask |
| `0005` | Workout catalog | Notify; subscribe then request `workouts` |

Notifications themselves are checked for authenticated encrypted connection state
by the adapter. Subscribe to result/status/catalog before requesting them. A GATT
write response confirms only receipt; a result message confirms application
acceptance or rejection. Commands cannot be sent with Write Without Response.
Info is intentionally readable before pairing and contains no session token.

## Framing

Each command write and each notification is one fragment. The eight-byte header
is followed by at least one payload byte. All 16-bit fields are little endian.

| Byte | Field |
| --- | --- |
| 0 | Framing version, currently 1 |
| 1 | Kind: command 1, result 2, status 3, catalog 4 |
| 2–3 | Nonzero message ID |
| 4–5 | Total unfragmented payload byte length |
| 6–7 | Offset of this fragment's payload |
| 8 onward | Payload bytes |

At the minimum ATT MTU of 23, one packet carries 12 payload bytes (20-byte ATT
value minus eight-byte header). Never assume a larger MTU was negotiated. Firmware
limits outgoing ATT values to 244 bytes and the negotiated MTU minus three.
Command payloads are printable ASCII, at most 191 bytes, assembled in exact order
within two seconds from the first fragment. The shared v1 codec additionally limits
ordinary commands to fewer than 160 bytes. A new offset-zero fragment restarts
assembly. Gaps, overlaps, invalid lengths/types/versions, and control characters
invalidate assembly without executing any command. Incoming message IDs correlate
results; the shared command sequence/epoch provide replay and stale-action checks.

Result, status and catalog payloads are UTF-8 JSON. Status and catalog IDs are
separate streams; reassemble by characteristic/kind and ID. Results echo request
IDs; a catalog echoes its `workouts` request ID. Status IDs are independently
incremented. The catalog is bounded by the 16-bit total length; an oversized catalog
request is explicitly rejected, not truncated. No payload may exceed 65,535 bytes.

Subscribe, then send these command payloads:

- `status`: request a full snapshot; no service registration or ownership needed.
- `workouts`: request the existing catalog; no service registration needed.
- `connect`: register this BLE session once and receive a token, client ID and epoch.
- `v1 TOKEN SEQUENCE EPOCH ACTION VALUE VALUE`: the existing shared text codec.

Use increasing nonzero sequences, the most recent control epoch, and the returned
16-character lowercase hexadecimal token. The adapter supplies the BLE transport
and peer identity; clients cannot impersonate another transport. See
[the shared contract](control-service.md) for actions, including `claim`, `start`,
`stop`, `targets`, `heartbeat`, `reset`, `release`, discrete buttons,
`cancel_workout`, and `w:WORKOUT_ID DURATION_MINUTES INTENSITY_PERCENT`.
A released session may claim again. A disconnected BLE link gets a fresh session
and token on reconnection. Pairing never automatically sends `connect` or `claim`.

There are eight ordinary queued requests and four priority Stop requests, with
bounded result storage per connection. Ordinary requests reserve at most seven
results, leaving an eighth slot for Stop. Exhausted buffers return ATT insufficient
resources (`0x11`) before execution. Restart the entire rejected message at offset
zero with the same ID/sequence after backoff; do not retry only its final fragment.
Do not retry ambiguous transport failures as new commands with new sequences.
After a lost result, query status and use shared replay protection.

Callbacks validate/frame/enqueue; only the worker submits to the controller
runtime. Stop is checked before pending ordinary work and retains runtime Stop
priority. The worker does not hold the adapter state mutex during runtime calls.
GAP callbacks and notification delivery use short state mutex sections, never
wait for control execution or relay pulses. Notification calls are serialized
on the NimBLE host event queue to prevent connection-handle reuse during a send.

Status snapshots begin no faster than five times per second. An in-flight snapshot
is completed before generating the newest one; slow clients do not accumulate old
snapshots. Each pass services results, status and catalog independently, so long
catalogs cannot block command replies. At minimum MTU, receiving a full snapshot
can take more than a second. Short relay pulses may not appear in every BLE status
snapshot; use USB monitoring to verify actual pulse masks during bench testing.

Requested speed/incline remain requested settings, not measured physical motion.
Boot ID and revision distinguish new controller boots and stale updates. App
suspension, lost heartbeats and disconnects retain the existing Stop/fault policy;
phone-lock improvements are deferred.

## Laptop-first test client

`tools/test-ble-client.py` uses Bleak; Linux USB-assisted pairing also uses pyserial
and a temporary BlueZ agent through Bleak's `dbus-fast` dependency. It does not
change host Wi-Fi. The default only reads info/status/catalog, without claiming
control or actuating outputs. Tokens and passkeys are excluded from output/reports.

```sh
python tools/test-ble-client.py --usb-pair /dev/ttyACM0 --report .local/ble-readonly.json
python tools/test-ble-client.py --address AA:BB:CC:DD:EE:FF --report .local/ble-reconnect.json
```

The first command opens the idle-only pairing window and supplies its code to a
temporary keyboard-capable BlueZ agent. It requires desktop/system BlueZ agent
permissions. Alternatively open the window through USB, pair using the operating
system prompt, and use `--pair`. Choose `--address` if more than one Nordic service
is nearby. Packet size 20 verifies small incoming write fragments; the negotiated MTU may
still be larger, and outgoing notifications use that negotiated value. This does
not prove both directions operated at MTU 23. `--packet-size 244`
is appropriate only when the link supports it.

Explicitly opting into the bench motion test:

```sh
python tools/test-ble-client.py --address AA:BB:CC:DD:EE:FF --exercise --full-workout --report .local/ble-exercise.json
```

This requires a stopped controller with ownership released, registers/claims BLE,
maintains heartbeats, exercises Start/Stop and both numeric directions, optionally
runs a complete one-minute hills workout with manual override and subsequent
segment reset, and finally checks stopped/released/relays-off state. The suite
surfaces heartbeat and cleanup failures. Run a parallel read-only USB monitor for
physical relay evidence; requested target acceptance alone is not relay validation.
Do not treat the suite as proof of physical speed, inclination, or machine safety.

## Board acceptance checklist (not satisfied by compiling)

1. Preserve the known-good image/config and NVS; flash the reviewed BLE-enabled
   Waveshare profile. Confirm original web controls and USB remain available.
2. Pair from the laptop over an idle-only USB window. Verify a wrong code and a
   closed/expired window reject enrollment. Verify bonding survives reconnect and
   reboot; verify forget removes authorization without erasing Wi-Fi settings.
3. Run read-only info/status/catalog with 20-byte packets, then repeat with the
   negotiated larger size. Check fragment errors, memory, crashes and boot ID.
4. Run the explicit exercise suite while USB observes Start, Stop, speed-up/down
   and incline-up/down pulse masks. Verify replayed/stale/malformed commands cannot
   actuate, full workout completion, manual override, cancel and fault reset.
5. Keep a phone on the existing webpage while the laptop observes BLE. Claim from
   web, confirm BLE cannot change settings, and send authorized observer Stop.
   Release and reverse roles. Confirm shared status and original webpage behavior.
6. Connect two BLE centrals (laptop plus iOS/Android diagnostic app). Verify observer
   Stop, ownership handoff and rejected third BLE connection without losing the
   first two clients. Nordic Semiconductor's nRF Connect for Mobile can discover,
   pair, subscribe and write characteristic bytes on both platforms; a custom app
   is not required, but manual writes must use the framing above.
7. Disconnect/kill the BLE owner and stop heartbeats separately. Check the existing
   fault/Stop behavior and deliberate reset/reclaim; no automatic restart. Exercise
   an observer disconnect without disturbing the owner.
8. Keep Wi-Fi page traffic and two BLE subscriptions active together. Measure
   responsiveness, heap, task stacks and link stability. ESP32 SoftAP and BLE share
   a radio; software coexistence does not by itself establish acceptable performance.

Record actual evidence and any failures; compiled firmware and host codec tests do
not establish radio coexistence, pairing security, relay contacts or mobile support.


## Verified Waveshare bench results

The reviewed BLE-enabled image was flashed and tested using the Linux laptop's
BlueZ/Bleak connection, with independent USB status monitoring. The existing phone
Wi-Fi page stayed connected during the successful workout; the user confirmed
that its values updated and it returned to stopped afterward.

| Check | Observed result |
| --- | --- |
| USB-assisted passkey enrollment | Successful authenticated pairing; subsequent bonded reconnect succeeded |
| Info, status, catalog | Read successfully; all six workouts received; no notification framing errors |
| Numeric controls | Start/Stop, speed changes in both directions, and incline changes in both directions passed |
| Full workout | One-minute hills workout completed; manual override was observed and cleared at the next segment; completion/elapsed-duration assertions passed |
| USB monitoring during workout | 920 snapshots, stable boot ID `2519712137`, fault 0 throughout; all six mapped relay output bits observed |
| Ownership rejection | Claim rejected with `control_in_use` while another client owned control |
| Observer Stop | USB stopped the BLE owner; after ownership handoff, BLE stopped the USB owner |
| Stale command protection | Old-epoch BLE command rejected after Stop |
| BLE owner link loss | Stopped with expected communication fault, no owner and relay mask 0; deliberate USB claim/reset/release restored idle, fault 0 |
| Final state | Idle, fault 0, ownership released, relay mask 0 |

The first two exercise attempts identified diagnostic-client bugs: Bleak 3 reports
ATT resource errors through a structured `.code`, and a fragmented status snapshot
can have started before the last accepted command. The client now retries only
resource backpressure with a bounded whole-message restart, and waits for a
same-boot snapshot at or beyond the accepted command revision. Regression tests
cover both cases. The full exercise passed after these client fixes; firmware did
not change between the failed diagnostic attempts and the successful rerun.

Local evidence is retained in ignored `.local/ble-readonly.json`,
`.local/ble-exercise.json`, `.local/ble-usb-monitor.jsonl`,
`.local/ble-cross-client-report.json`, and `.local/ble-disconnect-report.json`.
These reports exclude pairing passkeys and session tokens and are not committed.
Relay evidence is output-state acknowledgement, not proof of relay contact
operation, calibrated motion, or machine movement.

Remaining verification: two separate physical BLE centrals at once; iOS/Android
platform behavior and future custom apps; a longer Wi-Fi/BLE soak; and the negative
enrollment/removal cases in the checklist (wrong code, closed/expired window,
bond persistence across ESP reboot, and forget/re-pair). Incoming command writes
were deliberately limited to 20 bytes. BlueZ's reported MTU is not reliable proof
of the negotiated value, so actual MTU 23 in both directions remains unverified.

## Automated implementation checks

The initial implementation passed sanitized native C framing tests, existing
shared-service/contract/relay regression tests, and ten Python client tests with Bleak installed
(including the real Bleak 3 ATT resource error, bounded whole-message retry, and
failed heartbeat plus failed Stop still attempting release, and same-boot revision
barriers rejecting stale in-flight snapshots). Nine tests also run
without Bleak; the actual-library regression is skipped in that environment. ESP32 and
ESP32-S3 builds passed with BLE both enabled and disabled. The full Waveshare
BLE/web/relay image is `0x124b70` bytes, fitting the `0x177000` application partition
with 22% headroom. These build results do not replace the board checklist above.
