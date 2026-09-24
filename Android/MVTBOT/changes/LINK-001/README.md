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
suppressed while that MiniBalan session owns the connection. UI messages 3, 6
and 103 retain the existing screen behavior; dispatch checks the session
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
Notify requires CCCD 2902 and a successful descriptor-write callback. A failed
or incomplete connection closes within a total 10-second deadline.

Writes use 20-byte chunks, at least 30 ms apart, and a maximum of 24 pending
commands. Response writes wait for their callback and have a 750 ms timeout.
Queue overflow, rejected writes and callback errors disconnect and clear state.
These are transport outcomes and do not claim that the MCU executed a command.

MiniBalan receive data passes through the bounded `FrameDecoder` and schema
validation before the existing page parser. It is a byte stream: notification
boundaries carry no framing meaning. Disconnect/reconnect clears partial input.

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

## Reproducible checks

```powershell
python Android/MVTBOT/link/tests/test_smali_bridge.py
python Android/MVTBOT/link/tests/test_ble_session.py --java-home "$env:LOCALAPPDATA\Programs\AndroidTools\jdk-21"
```

The bridge replay checks input/output rejection and lifecycle hooks against the
archived smali. The BLE replay compiles the actual maintained Java with
deterministic Android fakes and checks session, GATT-selection, queue, framing
and control lifetime contracts. These checks do not replace Android verifier,
real-phone, real-module, background/foreground or vehicle acceptance tests.
