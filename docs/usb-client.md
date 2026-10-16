# USB diagnostic client

The bench firmware accepts a mock client through the ESP32-S3 USB Serial/JTAG
port. The computer retains its normal internet connection. No webpage changes
are required. HTTP and USB adapters both submit to `controller_runtime`;
neither adapter writes relays directly.

USB is transport 4. Physical access to this diagnostic USB port is the
authorization boundary, with ephemeral tokens scoped to the USB transport.
This adapter is enabled only for software-test builds using USB Serial/JTAG
as the console. It is not a production authentication scheme.

## Wire format

Send one ASCII line ending in LF (CRLF is also accepted):

```
1 status
2 connect
3 v1 TOKEN 1 EPOCH claim 0 0
4 v1 TOKEN 2 EPOCH heartbeat 0 0
```

The first number is a request ID from 1 through 1,000,000,000. Replies begin
with `@nordic ID ` followed by a JSON object. Ignore other serial lines, which
are boot and diagnostic logs. Send requests sequentially. `connect` registers
an observer and returns its token, client ID, and control epoch. It does not
claim ownership or activate outputs. Commands use the existing v1 codec;
increment sequence numbers and use the latest returned epoch.

`workouts` returns the shared preset catalog. Use `targets` with m/s and percent grade, or `w:ID` with duration minutes and intensity percent. Numeric values and workouts are accepted on relay builds with the same behavior as HTTP.

Status includes shared-service state and relay latch acknowledgments. It does
not measure physical contacts, machine speed, or incline. Malformed or oversized
lines are rejected. An oversized line is discarded through its newline.

Only one client owns control. An authorized observer can Stop. USB has the same
3-second command timeout as other clients; closing the serial client or unplugging
USB does not grant an unlimited lease. Status reads do not renew ownership.
Send heartbeat commands while controlling; use Stop and release when done.

## Local tests

The Python client requires pyserial (available in the existing ESP-IDF container).
Close any serial monitor before running it. Read-only check:

```
python tools/test-usb-client.py --port /dev/ttyACM0 --status-only
```

With relay terminals unloaded and the webpage control session released:

```
python tools/test-usb-client.py --port /dev/ttyACM0
```

The full test checks framing, registration, ownership, replay rejection, Start,
each speed/incline direction, Stop, command timeout, and manual reset. It verifies
output latch activation and release, and writes `.local/usb-client-report.json`.
This tests the real ESP service and relay driver, not browser JavaScript or HTTP.

For numeric targets plus an entire one-minute workout on the unloaded board:

```
python tools/test-usb-client.py --port /dev/ttyACM0 --restored-controls
```

This verifies all four target-change relay directions, manual override, the next
segment restoring preset targets, automatic completion, and cancellation. The
report is `.local/usb-restored-controls-report.json`.

Known diagnostic limitation: prolonged status monitoring has occasionally read
partial JSON replies across serial timeouts. A parse failure does not establish
the controller state; obtain a complete reply before interpreting the result.
Improved host-side reply buffering remains follow-up work.
