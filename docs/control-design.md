# Initial control design

## Application states

- `idle`: zero output demand; waiting for a deliberate start.
- `running`: valid operator commands may update requested speed and incline.
- `fault`: zero output demand in the simulator; start and setpoints rejected until reset.

The reference model has `start`, `set_targets`, `stop`, `trip`, `reset`, `heartbeat`, and `tick` operations. A future transport must validate commands before invoking them. It must never expose raw relay toggles in normal workout operation.

## Reference simulator behavior

- Startup is idle with zero targets.
- Start never restores a previous target.
- Heartbeats are required while running. Expiry is checked before accepting a late heartbeat or new setpoints.
- Communication expiry latches a fault. Reconnection alone cannot clear it.
- Reset clears a simulated fault but leaves the controller idle; a separate start is required.
- Invalid, nonfinite, and out-of-range setpoints are rejected without partially updating state.
- Motion ramps toward targets using elapsed time. There are no physical I/O writes.
- Stop and fault set simulated motion to zero immediately. **This is a model simplification, not a physical stopping strategy.**

The demo's 0–100 units, ramp rates, and two-second timeout are software exercise values only. They must not be copied into real machine limits without engineering and validation.

## Firmware requirements to resolve

The real application must run its timeout and control loop independently of the networking task. The drive should also have its own communication-loss behavior. Hardware outputs need defined inactive states during boot, reset, and power loss, including before the application starts.

Determine the actual controlled stopping sequence, lift holding behavior, direction interlocks, feedback plausibility checks, and fault reset conditions from the machine design. Ordinary ESP firmware is not the independent safety circuit.

## Network plan

Start with local Wi-Fi and a tablet web interface; support a protected setup access point and connection to an existing local network. Internet connectivity should not be required for operation. Add BLE setup/native-app transport after the Wi-Fi milestone.

Before enabling hardware commands: require explicit operator pairing, one active controlling session, bounded and validated messages, stale-command rejection, and no automatic restart after a connection returns. Keep credentials outside source control. OTA firmware updates must be restricted to an idle, disabled machine and must preserve safe boot behavior.
