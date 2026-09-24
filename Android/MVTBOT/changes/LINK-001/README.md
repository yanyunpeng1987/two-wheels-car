# LINK-001 / LINK-002 / LINK-003 Android bridge

This change keeps the v21 UI and takes over BLE only when the user explicitly
selects MiniBalan. It does not add Classic Bluetooth/SPP, automatically connect
to scan results, or identify a product from its advertised name.

## Build inputs

- `../../link/src/com/mvtbot/link/`: maintained Java, compiled against Android
  API 36 with Java 8 bytecode; D8 minimum API 23. No original application classes
  are compilation dependencies.
- `patch_smali.py --smali-root <isolated decoded project/smali>`: eleven-file
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

The compatibility session centrally owns voltage health queries. The legacy
approximately-one-second UI CMD7 ticker coalesces into that owner, with at most
one query queued, writing or awaiting a notification. Stop has highest priority;
already-queued motion precedes the health query, and newer motion cannot
continually overtake that queued query. Ordinary configuration follows, and an
in-progress frame is never interleaved. A query is due after 1000 ms. Its actual
submission starts a 1500 ms round-trip deadline, while 2500 ms without a fresh
qualified CMD7 independently closes the session and clears movement. Both a
write completion and a valid voltage notification are required before the query
slot is released. The health clock uses notification arrival, not a later write
callback. Duplicate/out-of-window, invalid, partial and non-voltage frames do not
refresh it. The presentation decoder is separate, so resetting query assembly
does not erase a fragmented PID response.

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

## Optional local diagnostic auto-connect

Normal builds remain manual. A diagnostic build may contain exactly one local
asset, `assets/mvtbot-debug-autoconnect.properties`, with two ASCII properties:
`enabled=true` and `targetAddress=<explicitly verified local device address>`.
The target is supplied locally to the build; it is not a source-code default or
a tracked configuration. Missing, disabled or invalid configuration never scans.
Application logs omit the address. `DebugAutoConnect.java` is ordinary maintained
source included with the other link classes, without an additional smali hook.

After `bind`, MiniBalan selection and 500 ms of stable foreground readiness,
this option makes one BLE scan attempt per application process, with an exact-address platform
filter and a 10-second monotonic deadline. A real scan result must match that
address again before the scanner stops and the normal connection method runs.
Device names and RSSI never choose a replacement; no cached or paired-device
record alone triggers connection. Individual and batched results share the same
guard. No match, scan failure, missing permission or disabled Bluetooth consumes
the attempt without retrying. Another attempt requires a new process.

An initial foreground wait interrupted by `onPause` (such as the startup
landscape configuration change) is cancelled without consuming the scan attempt;
`onResume` may schedule a new full 500 ms wait. A manual connection/disconnection,
leaving MiniBalan or Activity destruction also cancels a pending wait and consumes
the attempt. Backgrounding after the actual scan starts consumes it too.
Tokens reject queued preparation tasks, results, failures and timeouts after
cancellation; once the attempt is consumed, later resume cannot reconnect.
An automatic connection retains the same profile proof, startup zeros, health
queries, manual-disconnect behavior and prohibition on replaying prior motion.
This is a diagnostic convenience, not a replacement for selecting and verifying
the local vehicle or a new production auto-reconnect policy.

## Reproducible checks

```powershell
python Android/MVTBOT/link/tests/test_smali_bridge.py
python Android/MVTBOT/link/tests/test_ble_session.py --java-home "$env:LOCALAPPDATA\Programs\AndroidTools\jdk-21"
python Android/MVTBOT/link/tests/test_debug_autoconnect.py --java-home "$env:LOCALAPPDATA\Programs\AndroidTools\jdk-21"
python Android/MVTBOT/link/tests/test_startup_subscriptions.py --java-home "$env:LOCALAPPDATA\Programs\AndroidTools\jdk-21" --cc <path-to-zig.exe>
```

The bridge replay checks input/output rejection and lifecycle hooks against the
archived smali. The BLE replay compiles the actual maintained Java with
deterministic Android fakes and checks session, GATT-selection, queue, framing
and control lifetime contracts. These checks do not replace Android verifier,
real-phone, real-module, background/foreground or vehicle acceptance tests.

The v32 BLE suite passes 444 assertions, including both health callback orders,
slow callbacks, startup cancellation, bounded priority requests and PID echo
suppression. The startup test feeds timestamped writes from the actual Java
through the actual MCU `bluetooth.c` and `bluetooth_link.c` with the HAL fixture.
It starts with reporting `[0,0,1]`, checks both ISR/main-loop orders and receive
delays, and records 38 schedules plus 9 Java assertions in
`build/hc05d-validation/startup-subscriptions/report.json`. All tested 60 ms
callback schedules and the 180 ms cases without extra UART delivery delay
finish at `[1,1,0]`. A separate legacy-boundary fixture demonstrates how a
subscription can be lost at the 500 ms epoch transition while error counters
remain zero; it is not a reconstruction of the exact phone incident.

The same report preserves two incomplete edge cases: 180 ms callbacks with
40 ms additional UART delivery delay and ISR before the main loop can lose CMD5
at that boundary, leaving `[1,0,0]`. The BLE suite also measures motion gaps of
540/720/900 ms for response callbacks delayed by 270/360/450 ms. Such links exceed
the unchanged MCU 500 ms motion lease and correctly stop movement. The new
startup refresh and HIGH requests therefore require real negotiated-parameter,
telemetry and motion checks; passing a health query does not certify continuous
control on an arbitrarily slow link.

The diagnostic auto-connect replay uses synthetic addresses and separate JVMs
for process-lifetime scenarios. It checks disabled configuration, exact result
filtering, deadline expiry before the timeout Runnable executes, denied/failed
scans, manual-operation priority, lifecycle cancellation, the cold-start
100 ms pause/220 ms resume sequence, and the actual
MiniBalan handshake through the automatic entry point. These host tests do not
scan or connect to a radio device.
