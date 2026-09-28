# LINK-001 / LINK-002 / LINK-003 Android bridge

This change keeps the v21 UI and takes over BLE only when the user explicitly
selects MiniBalan. It does not add Classic Bluetooth/SPP, automatically connect
to scan results, or identify a product from its advertised name.

## Build inputs

- `../../link/src/com/mvtbot/link/`: maintained Java, compiled against Android
  API 36 with Java 8 bytecode; D8 minimum API 23. No original application classes
  are compilation dependencies.
- `patch_smali.py --smali-root <isolated decoded project/smali>`: twelve-file
  bridge with exact v21 input hashes in `smali-baseline.json`. Only LF/CRLF is
  normalized. It checks all inputs before mutation. A second application verifies
  the complete output manifest and does nothing. A changed script or output
  requires a fresh decode of the immutable DEX baseline.
- The raw v21 `project/` and archived analysis are references, never patch
  destinations. New Java and patched smali must actually be assembled into the
  final DEX before the existing align/sign/package checks.

## Composition with the approved UI update

The integrated release uses APP-UI-001 from commit
`aa000240b183a8daeed3f09ca084cdf9335e0076`. First apply its exact five resource
edits and three smali edits to a fresh v21 snapshot, then run:

```powershell
python Android/MVTBOT/changes/LINK-001/patch_smali.py --smali-root <snapshot/smali> --ui-patch Android/MVTBOT/changes/APP-UI-001/patch.json
```

The UI JSON is pinned by its normalized SHA-256
`953db3d574a712fa707e11ba192c28d59f3758f8c80f3a2808016a423e9e1eef`.
The shared `MainActivity` must exactly match that UI patch's output: LINK
reverses the three UI replacements, checks the reconstructed immutable v21
hash, and replays them forward. All other LINK inputs still require their
original v21 hashes. The two UI-only smali classes must match their pinned UI
output hashes and remain unchanged. LF/CRLF differences are permitted for
smali; the original UI resource edits preserve raw XML bytes, including mixed
line endings inside string tables.

In the integrated `selectDevice`, LINK inserts its session hook after the UI
patch's unique `p1 = MINIBALAN` assignment. It uses that fixed mode rather than
the incoming saved/selected robot, preserving fixed MiniBalan selection,
preference updates, drawer locking, immediate startup and company/website UI.
No whole-method UI replacement is applied after LINK.

The output manifest records the UI source commit, profile hash, actual inputs
and outputs. Idempotent replay must supply the same UI profile; dropping the
flag, changing the UI JSON, or altering combined/UI-only output is rejected.
Omitting `--ui-patch` on a fresh v21 snapshot retains the standalone LINK mode
for comparison. Resource/version/final-APK validation remains a build gate.

## Session contract

`MainActivity.onCreate` passes its actual handler and Context to
`MiniBalanLink.bind`. `BLEManager.connect` routes explicitly selected MiniBalan
to the new GATT owner, closes the legacy service, and otherwise calls its
preserved v21 implementation. Legacy receiver and queued sender callbacks are
suppressed while that MiniBalan session owns the connection. UI messages 3, 4,
6 and 103 retain connected/setup-failed/disconnected/telemetry behavior; dispatch checks the session
generation immediately before entering the original handler. The final
`MainActivity$MsgCallBack` wrapper requires an active private dispatch identity,
generation and valid frame for MiniBalan data; merely posting an old message 3,
6 or 103 cannot bypass readiness or validation. UI ticker message 20 remains.
Selecting MiniBalan claims its transport path before the device picker, preventing
an existing connection to another robot from becoming this page's connection.

The legacy background sender captures its generation across sleeps. Its next
ByteBuffer send or recovery cannot affect a new session. Legacy GATT write,
connect and reconnect calls use a short lock shared with ownership takeover;
the lock never spans a command delay. Legacy worker stop calls cannot close the
new GATT. Other robot operation resumes when explicitly leaving MiniBalan.

Service selection prefers a complete E0FF service over FFE0. Both require FFE1
write and notify handles within that service. A single characteristic can serve
both roles; separate FFE1 characteristics are accepted when their properties
unambiguously identify each role. Multiple eligible handles/services are
rejected. No-response write is preferred; response write is a serial fallback.
Notify requires CCCD 2902 and a successful descriptor-write callback. This only
opens the internal write gate. Controls, telemetry presentation and the connected
message become operational after a successful initial zero write, or the
strictly qualified voltage round trip and startup neutral refresh below. The first write waits for Android's
completion callback in either advertised write mode. Failed or incomplete setup,
including compatibility identification and the probe,
below, closes within a total 10-second deadline and reports connection failure.
The MiniBalan icon is redrawn from this operational state during both connection
events and page refreshes; the original always-connected icon rule is bypassed.

Writes use 20-byte chunks, at least 30 ms apart, and a maximum of 24 pending
commands. Response writes wait for their callback and have a 750 ms timeout.
Their completion immediately pumps the next command; the submission clock
enforces the 30 ms minimum without adding another delay after a slow callback.
Queue overflow, rejected writes and callback errors outside the measured
exception below disconnect and clear state.
These are transport outcomes and do not claim that the MCU executed a command.

Each session requests Android connection priority HIGH at most twice: once after
connection and once 1000 ms after becoming operational. An accepted API call is
not evidence of negotiated connection parameters or guaranteed radio latency.

### Qualified HC-05D 1.1.4 completion behavior

Independent PC tests found a specific module reports ATT status 13 for multi-byte
response writes after forwarding the original bytes to UART. Repeated voltage
and PID queries returned valid data despite that status. One-byte response writes
worked; no-response writes did not reach UART. The earlier fixed-20-byte trial
also returned 13 and has been removed from current code and tests. Its phone
evidence remains under `build/hc05d-device-validation/20260924-182933/phone-v28-first-logcat.txt`;
PC round-trip evidence remains in `pc-response-roundtrips.json` in that directory.

The per-session state machine is `NORMAL -> IDENTIFYING -> PROBING -> VALIDATED`:

- Only status 13 on the very first 11-byte zero, response write type 2, same
  FFE1 write/notify characteristic with exact properties `0x18`, and service
  **FFE0** may start identification. The zero is not retried. E0FF retains its
  normal standards-based path and does not inherit this unverified exception.
- Serial, non-identifying DIS reads must match manufacturer `QUALCOMM` (2A29),
  model `HC-05D` (2A24), and firmware `1.1.4` (2A26) byte-for-byte. Missing,
  ambiguous, unreadable, different, timed-out or failed reads close the session.
  This is a compatibility allowlist, not device security authentication.
- Send one original, unpadded `CMD|7|$`. Operational state requires both its
  write callback (0 or the qualified 13) and a fresh complete voltage response
  in the inclusive range 0..40000 mV. Either arrival order is allowed. A separate
  decoder resets at actual query submission, and notification arrival timestamps
  reject data already received before submission. No UI connected event or
  movement is enabled by CCCD, matching DIS, or a write callback alone.
- After that round trip validates this profile, send two new independent zero
  commands before publishing operational state. The second is submitted at
  least 100 ms after the first completes. This refreshes the MCU control lease
  before the original page starts its CMD4/5/6 subscriptions. Cancellation,
  replacement and setup timeout invalidate the startup token. These zeros do
  not refresh voltage health; if its 2500 ms limit has elapsed, setup fails.
- After validation, status 13 is adapted only for response writes of 2..20 actual
  bytes belonging to a complete validated outgoing command in this same profile.
  One-byte status 13, other error codes, or a different profile still fail closed.
  Data is not padded, the write mode does not change, and commands are not retried.
  Observed/adapted status-13 counters and data/wire lengths remain diagnostic.

The compatibility session centrally owns voltage queries and coalesces the legacy
UI ticker. A query becomes due after 1000 ms, may wait in the queue for at most
1500 ms, and receives its own 1500 ms deadline after actual submission. ATT
writes retain the independent 750 ms deadline. A fresh complete CMD7 and its
write completion are both required; notification time, not callback time, is
used for freshness.

The former 2500 ms freshness cutoff now suspends old motion instead of closing
a still-budgeted query. A completed write with one missing reply permits exactly
one read-only CMD7 retry; repeated loss, stalled queue or stalled write closes
within its deadline. Nonzero motion and parameter writes are never replayed.
Recovery latches a neutral/release requirement: held input cannot restart motion,
including through reset callback re-entry. After a new proof, the user must
release the joystick or return gravity input to neutral before the normal double
zero rearm. Foreground users get one pause/recovery notice per recovery cycle;
background and stale sessions do not produce notices. The GATT-connected icon
remains connected during bounded recovery.

A separate decoder resets at actual query submission. Old, partial, invalid or
non-voltage frames cannot satisfy the health proof. Diagnostics record queue,
submission, notification and callback times plus close and cleanup outcomes.

Connection replacement/disconnect clears the profile, pending reads, query state
and counters. The protocol has no request IDs; single-query ownership and local
timestamps reduce but cannot eliminate ambiguity from a delayed old response.
This proof establishes the communication path, not per-motion acknowledgement or
fresh ADC conversion (the MCU can report its last valid voltage sample).

MiniBalan receive data passes through the bounded `FrameDecoder` and schema
validation before the existing page parser. It is a byte stream: notification
boundaries carry no framing meaning. Disconnect/reconnect clears partial input.
During synchronous presentation of validated telemetry, CMD1/CMD2 sends caused
by old UI value-change callbacks are handled without transmitting. This prevents
PID readback from writing the same settings back to the MCU. Actual user changes
outside that presentation scope, including arrow buttons, retain their sends.

## Control lifetime

Only the visible control view can supply motion; generic command send APIs
cannot arm a nonzero CMD3. Motion is normalized to -1/0/1, and a fresh gesture
starts with two separate zero commands. After the first completes, at least
100 ms elapses before the second is dispatched; movement waits until the second
completes. This accommodates the MCU's conservative rejection of a DMA buffer
armed before the previous control lease expired. Each completion checks the
connection generation, current view owner and gesture token before changing
arming state. Release, pause, owner replacement and reconnect invalidate both
phases; an old completion cannot unlock a newer gesture. Subsequent inputs replace
the latest direction; the heartbeat sends it every 100 ms. Release, switching
tabs/modes, hidden views and Activity pause clear direction and pending motion,
send a best-effort zero and stop the heartbeat. Reconnection never restores old
movement. Gravity control requires one neutral reading (both axes <=3.0) after
a session/mode/lifecycle reset. Ordinary zero samples keep that gate open; each
sample computes a complete vector with a symmetric 3.5 movement threshold,
including an explicit zero through the old 3.0-to-3.5 rounding gap.

The control fragment implements a small owned `ControlReset` interface, so
queued state is cleared without reflection or a dependency on decompiled Java.
Motion and gravity inputs carry the current control-view owner identity; events
from destroyed or replaced pages are rejected. Sensor listeners are unregistered
on pause/view destruction and re-registered by the selected mode on resume.
Radio failure may prevent the zero from arriving; the device firmware watchdog
is the authoritative bound on retained motion. User disconnect allows at most
200 ms for a queued zero, then closes.

BLE scanning remains manual. For the MiniBalan picker, observed bonded and
unnamed BLE devices are shown; unnamed devices have a label and retain the
existing address display. Other robot scanning and protocol paths retain v21
behavior. Receiver unregistration uses its saved registration context and is
idempotent, including an already-unregistered receiver. Manager destruction
unregisters before clearing its receiver/context, so service disconnection
cannot leave the static registration flag blocking a later manager instance.

## Manual scanning and removal of startup auto-connect

The v36 diagnostic auto-connect helper and target asset were removed at the
user's request. Current packaging rejects the old asset and any residual helper
class. No scan or connection starts just from launching/resuming MiniBalan.

`ManualBleScanner` owns one modern Android scan. The Mini picker is constructed
once and starts only after it is shown. Selection, dismiss/outside cancel,
background, destroy and explicit connection stop scanning. Session identity
rejects late callbacks; a cancellable 10-second timeout belongs to that scan.
A repeated show is idempotent and an old picker cannot stop its replacement.
Other robot picker methods retain the original v21 implementations.

The scanner limits its own requests to four starts per 30 seconds with at least
6.5 seconds between starts. These are application policy, not a claim about a
particular phone's system quota. Async failure 6 produces a 30-second cooldown;
other scan failures use 6.5 seconds. The user sees the failure/wait state and
explicitly requests a new scan. There is no automatic retry or automatic
selection by name/RSSI.

## Reproducible checks

```powershell
python Android/MVTBOT/link/tests/test_smali_bridge.py
python Android/MVTBOT/link/tests/test_ble_session.py --java-home "$env:LOCALAPPDATA\Programs\AndroidTools\jdk-21"
python Android/MVTBOT/link/tests/test_health_recovery.py --java-home "$env:LOCALAPPDATA\Programs\AndroidTools\jdk-21"
python Android/MVTBOT/link/tests/test_manual_scanner.py --java-home "$env:LOCALAPPDATA\Programs\AndroidTools\jdk-21"
python Android/MVTBOT/link/tests/test_startup_subscriptions.py --java-home "$env:LOCALAPPDATA\Programs\AndroidTools\jdk-21" --cc <path-to-zig.exe>
```

The bridge replay checks input/output rejection and lifecycle hooks against the
archived smali. The BLE replay compiles the actual maintained Java with
deterministic Android fakes and checks session, GATT-selection, queue, framing
and control lifetime contracts. These checks do not replace Android verifier,
real-phone, real-module, background/foreground or vehicle acceptance tests.

The v38 suite passes 446 BLE, 618 health-recovery and 97 manual-scanner checks.
The startup test feeds actual Java writes through actual MCU source under 38
schedules. All now finish at reporting [1,1,0], including the former epoch-drop
regression and slow delivery offsets. The separate cross-stack rearm replay
covers five scenarios under both MCU processing orders (64 MCU frame assertions
and 53 Java assertions).

The serial ATT bandwidth limitation remains: simulated callback delays of
270/360/450 ms can produce motion gaps of 540/720/900 ms when a separate query
interleaves. The MCU 500 ms lease remains unchanged. Passing bounded recovery
and query tests does not establish physical continuous-motion performance;
that requires the actual paired APK/firmware and device validation.
