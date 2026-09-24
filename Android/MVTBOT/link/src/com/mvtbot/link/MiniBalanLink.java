package com.mvtbot.link;

import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothProfile;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.os.Message;
import android.util.Log;
import android.view.View;
import java.lang.ref.WeakReference;
import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Iterator;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.locks.ReentrantLock;

/** LINK-001/002/003: dedicated BLE session for the explicitly selected MiniBalan page.
 * All mutable session and GATT state belongs to the main looper. No original app
 * classes are compile dependencies. The versioned smali bridge supplies the UI handler.
 */
public final class MiniBalanLink {
    public interface ControlReset { void resetMotion(); }
    private static final String TAG = "MVTBOT-Link";
    private static final UUID E0FF = uuid("e0ff"), FFE0 = uuid("ffe0"), FFE1 = uuid("ffe1"), CCCD = uuid("2902");
    private static final String ZERO = "CMD|3|0|0|$";
    private static final int MAX_QUEUE = 24, ATT_PAYLOAD = 20;
    private static final long CONNECT_TIMEOUT_MS = 10000, WRITE_TIMEOUT_MS = 750, WRITE_GAP_MS = 30, HEARTBEAT_MS = 100, REARM_ZERO_GAP_MS = 100;
    private static final Handler LOOP = new Handler(Looper.getMainLooper());
    private static final ReentrantLock LEGACY_GATE = new ReentrantLock();
    private static final ThreadLocal<Long> LEGACY_DISPATCH = new ThreadLocal<>();
    private static final ThreadLocal<Message> UI_EVENT = new ThreadLocal<>();
    private static final int UI_MARKER = 0x4d56544c;
    private static volatile boolean owned, ready;
    private static volatile long generation;
    private static volatile String name = "";
    private static Context context;
    private static WeakReference<Handler> ui = new WeakReference<>(null);
    private static WeakReference<ControlReset> control = new WeakReference<>(null);
    private static WeakReference<View> controlView = new WeakReference<>(null);
    private static boolean foreground, heartbeat, armPending, gravityControl, gravityNeutralRequired = true;
    private static long gestureToken;
    private static String latestMotion = ZERO;
    private static BluetoothGatt gatt;
    private static BluetoothGattCharacteristic write, notify;
    private static BluetoothGattDescriptor cccd;
    private static int writeType;
    private static boolean waitingWrite;
    private static long writeTicket;
    private static Tx active;
    private static final ArrayDeque<Tx> queue = new ArrayDeque<>();
    private static final FrameDecoder decoder = new FrameDecoder(new FrameDecoder.Listener() {
        public void onFrame(String frame) {
            // The validated protocol accepts an optional final separator; the v21
            // presentation parser requires it to keep '$' out of numeric fields.
            String presentation = frame.endsWith("|$") ? frame : frame.substring(0, frame.length() - 1) + "|$";
            emit(103, presentation, generation);
        }
        public void onRejected(String reason) {
            Log.w(TAG, "Rejected telemetry: " + reason + " count=" + decoder.getRejectedCount()
                + " raw=" + hex(decoder.getLastRejectedBytes()));
        }
    });

    private MiniBalanLink() { }
    private static UUID uuid(String shortId) { return UUID.fromString("0000" + shortId + "-0000-1000-8000-00805f9b34fb"); }
    public static boolean isOwned() { return owned; }
    public static boolean isReady() { return owned && ready; }
    public static String deviceName() { return name; }

    public static void bind(Context appContext, Handler handler) {
        context = appContext.getApplicationContext();
        ui = new WeakReference<>(handler);
    }
    /** Claim the explicitly selected MiniBalan path even before a device is picked.
     * A still-connected legacy robot must not become this page's transport.
     */
    public static void selectMini() {
        if (owned) return;
        setOwned(true);
        generation++;
        resetControl();
    }
    public static boolean connect(BluetoothDevice device) { return connect(context, ui.get(), device); }

    public static boolean connect(Context appContext, Handler handler, BluetoothDevice device) {
        if (appContext == null || handler == null || device == null) return false;
        // Called by the device picker on the main thread; marking ownership first
        // suppresses late broadcasts from the legacy BLE service being closed.
        LEGACY_GATE.lock();
        try { owned = true; } finally { LEGACY_GATE.unlock(); }
        context = appContext.getApplicationContext();
        ui = new WeakReference<>(handler);
        run(() -> open(device));
        return true;
    }

    private static void open(BluetoothDevice device) {
        closeGatt();
        final long session = ++generation;
        resetControl();
        name = displayName(device);
        emit(6, null, session); // pending discovery is not an operational connection
        try {
            gatt = device.connectGatt(context, false, callback(session), BluetoothDevice.TRANSPORT_LE);
            if (gatt == null) { fail("connectGatt returned null"); return; }
        } catch (RuntimeException e) { fail("connect denied: " + e.getClass().getSimpleName()); return; }
        LOOP.postDelayed(() -> { if (current(session) && !ready) fail("connection/CCCD timeout"); }, CONNECT_TIMEOUT_MS);
    }

    private static BluetoothGattCallback callback(final long session) {
        return new BluetoothGattCallback() {
            @Override public void onConnectionStateChange(BluetoothGatt source, int status, int state) {
                LOOP.post(() -> {
                    if (!current(session, source)) return;
                    if (status != BluetoothGatt.GATT_SUCCESS || state == BluetoothProfile.STATE_DISCONNECTED) {
                        fail("GATT disconnected/status=" + status); return;
                    }
                    if (state == BluetoothProfile.STATE_CONNECTED) {
                        try { if (!source.discoverServices()) fail("service discovery rejected"); }
                        catch (RuntimeException e) { fail("service discovery denied"); }
                    }
                });
            }
            @Override public void onServicesDiscovered(BluetoothGatt source, int status) {
                LOOP.post(() -> {
                    if (!current(session, source)) return;
                    if (status != BluetoothGatt.GATT_SUCCESS) { fail("service discovery failed"); return; }
                    try {
                        Pair pair = select(source.getServices());
                        if (pair == null) { fail("missing or ambiguous FFE1 write/notify characteristics"); return; }
                        write = pair.write; notify = pair.notify;
                        writeType = (write.getProperties() & BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE) != 0
                            ? BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE : BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT;
                        cccd = notify.getDescriptor(CCCD);
                        if (cccd == null || !source.setCharacteristicNotification(notify, true)) {
                            fail("notification subscription unavailable"); return;
                        }
                        cccd.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
                        if (!source.writeDescriptor(cccd)) fail("CCCD write rejected");
                    } catch (RuntimeException e) { fail("service/CCCD setup denied"); }
                });
            }
            @Override public void onDescriptorWrite(BluetoothGatt source, BluetoothGattDescriptor descriptor, int status) {
                LOOP.post(() -> {
                    if (!current(session, source) || descriptor != cccd || ready) return;
                    if (status != BluetoothGatt.GATT_SUCCESS) { fail("CCCD write failed"); return; }
                    ready = true;
                    decoder.reset();
                    resetControl();
                    enqueueStop(null);
                    if (!current(session, source) || !ready) return;
                    emit(3, null, session);
                    LOOP.postDelayed(() -> tick(session), HEARTBEAT_MS);
                    Log.i(TAG, "MiniBalan ready; service=" + write.getService().getUuid() + " writeType=" + writeType);
                });
            }
            @Override public void onCharacteristicChanged(BluetoothGatt source, BluetoothGattCharacteristic characteristic) {
                byte[] bytes = characteristic.getValue();
                acceptNotification(source, characteristic, bytes == null ? null : bytes.clone(), session);
            }
            @Override public void onCharacteristicChanged(BluetoothGatt source, BluetoothGattCharacteristic characteristic, byte[] value) {
                acceptNotification(source, characteristic, value == null ? null : value.clone(), session);
            }
            @Override public void onCharacteristicWrite(BluetoothGatt source, BluetoothGattCharacteristic characteristic, int status) {
                LOOP.post(() -> {
                    if (!current(session, source) || characteristic != write || !ready) return;
                    if (status != BluetoothGatt.GATT_SUCCESS) { fail("characteristic write failed"); return; }
                    if (writeType == BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT && waitingWrite) {
                        waitingWrite = false;
                        writeTicket++;
                        LOOP.postDelayed(MiniBalanLink::pump, WRITE_GAP_MS);
                    }
                });
            }
        };
    }

    private static void acceptNotification(BluetoothGatt source, BluetoothGattCharacteristic characteristic, byte[] value, long session) {
        LOOP.post(() -> {
            if (current(session, source) && ready && characteristic == notify && value != null) decoder.feed(value);
        });
    }

    private static final class Pair {
        final BluetoothGattCharacteristic write, notify;
        Pair(BluetoothGattCharacteristic w, BluetoothGattCharacteristic n) { write = w; notify = n; }
    }
    private static Pair select(List<BluetoothGattService> services) {
        // Prefer a complete E0FF service, otherwise a complete FFE0. Never mix handles
        // across services and never silently choose among duplicate candidates.
        for (UUID serviceId : new UUID[] { E0FF, FFE0 }) {
            List<Pair> candidates = new ArrayList<>();
            for (BluetoothGattService service : services) {
                if (!serviceId.equals(service.getUuid())) continue;
                List<BluetoothGattCharacteristic> noResponse = new ArrayList<>(), response = new ArrayList<>(), notifications = new ArrayList<>();
                for (BluetoothGattCharacteristic c : service.getCharacteristics()) {
                    if (!FFE1.equals(c.getUuid())) continue;
                    int props = c.getProperties();
                    if ((props & BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE) != 0) noResponse.add(c);
                    else if ((props & BluetoothGattCharacteristic.PROPERTY_WRITE) != 0) response.add(c);
                    if ((props & BluetoothGattCharacteristic.PROPERTY_NOTIFY) != 0 && c.getDescriptor(CCCD) != null) notifications.add(c);
                }
                List<BluetoothGattCharacteristic> writes = noResponse.isEmpty() ? response : noResponse;
                if (writes.size() > 1 || notifications.size() > 1) throw new IllegalArgumentException("ambiguous characteristics");
                if (writes.size() == 1 && notifications.size() == 1) candidates.add(new Pair(writes.get(0), notifications.get(0)));
            }
            if (candidates.size() > 1) throw new IllegalArgumentException("ambiguous services");
            if (candidates.size() == 1) return candidates.get(0);
        }
        return null;
    }

    /** Non-motion commands only. Motion is accepted solely from a live control view. */
    public static boolean send(String command) {
        if (LEGACY_DISPATCH.get() != null) return false;
        if (!isReady() || !ProtocolValidation.validCommand(command)) return false;
        final long session = generation;
        final int[] motion = ProtocolValidation.motion(command);
        if (motion != null) {
            if (motion[0] == 0 && motion[1] == 0) { release(); return true; }
            return false;
        }
        run(() -> { if (current(session) && ready) enqueue(new Tx(command, false, false, null)); });
        return true;
    }
    public static boolean sendBytes(byte[] command) {
        return command != null && send(new String(command, StandardCharsets.US_ASCII));
    }

    public static void bindControl(ControlReset owner, View view) {
        run(() -> { halt(); control = new WeakReference<>(owner); controlView = new WeakReference<>(view); gravityControl = false; resetControl(); });
    }
    public static void unbindControl(ControlReset owner) {
        run(() -> { if (control.get() == owner) { halt(); control.clear(); controlView.clear(); } });
    }
    public static void foreground(boolean value) {
        run(() -> { foreground = value; if (!value) halt(); });
    }
    public static void release() { run(MiniBalanLink::halt); }
    public static void pageChanged() { release(); }

    public static boolean isControlOwner(ControlReset owner) { return owner != null && control.get() == owner; }
    public static void controlMode(ControlReset owner, int mode) {
        if (!isControlOwner(owner)) return;
        gravityControl = mode == 1;
        halt();
    }
    /** A complete vector for every sample; a 3.5 dead zone closes the v21 rounding gap.
     * Neutral (<=3.0) is required once after a session/mode/lifecycle reset, not
     * after every ordinary zero sample. Return value preserves joystick indication.
     */
    public static int gravity(ControlReset owner, float x, float y) {
        if (!isControlOwner(owner) || !gravityControl || !isReady() || !visible()) return -2;
        if (Float.isNaN(x) || Float.isNaN(y) || Float.isInfinite(x) || Float.isInfinite(y)) { halt(false); return -2; }
        if (gravityNeutralRequired) {
            if (Math.abs(x) <= 3f && Math.abs(y) <= 3f) gravityNeutralRequired = false;
            halt(false);
            return -2;
        }
        int move = 0, turn = 0, direction = -2;
        if (Math.abs(x) > Math.abs(y)) {
            if (Math.abs(x) >= 3.5f) { move = x < 0 ? 1 : -1; direction = x < 0 ? 0 : 2; }
        } else if (Math.abs(y) >= 3.5f) { turn = y < 0 ? -1 : 1; direction = y < 0 ? 3 : 1; }
        motion(owner, move, turn);
        return direction;
    }
    public static void motion(ControlReset owner, int move, int turn) {
        final String frame = "CMD|3|" + Integer.signum(move) + "|" + Integer.signum(turn) + "|$";
        run(() -> {
            if (!isControlOwner(owner)) return;
            if (!ready || !visible()) { resetControl(); return; }
            if (move == 0 && turn == 0) { halt(false); return; }
            latestMotion = frame;
            if (!heartbeat) {
                heartbeat = true;
                armPending = true;
                final long session = generation, gesture = ++gestureToken;
                // A quiet MCU may discard the first post-timeout DMA completion
                // because that receive buffer was armed in the previous epoch.
                // Two separate zero dispatches avoid unlocking from that old data.
                enqueueStop(() -> {
                    if (!armingCurrent(session, owner, gesture)) return;
                    LOOP.postDelayed(() -> {
                        if (!armingCurrent(session, owner, gesture)) return;
                        enqueueStop(() -> {
                            if (!armingCurrent(session, owner, gesture)) return;
                            armPending = false;
                            enqueueMotion();
                        });
                    }, REARM_ZERO_GAP_MS);
                });
            } // Further input replaces latestMotion; the 100 ms heartbeat sends it.
        });
    }
    private static void tick(long session) {
        if (!current(session) || !ready) return;
        if (heartbeat) {
            if (!visible()) halt();
            else if (!armPending) enqueueMotion();
        }
        LOOP.postDelayed(() -> tick(session), HEARTBEAT_MS);
    }
    private static boolean visible() {
        View view = controlView.get();
        return foreground && view != null && view.isShown() && view.getWindowVisibility() == View.VISIBLE;
    }
    private static boolean armingCurrent(long session, ControlReset owner, long gesture) {
        return current(session) && ready && heartbeat && armPending && visible()
            && isControlOwner(owner) && gestureToken == gesture;
    }
    private static void resetControl() {
        resetControl(true);
    }
    private static void resetControl(boolean requireNeutral) {
        gestureToken++;
        heartbeat = false; armPending = false; latestMotion = ZERO;
        if (requireNeutral) gravityNeutralRequired = true;
        ControlReset owner = control.get();
        if (owner != null) owner.resetMotion();
    }
    private static void halt() { halt(true); }
    private static void halt(boolean requireNeutral) {
        boolean wasMoving = heartbeat || armPending || !ZERO.equals(latestMotion);
        resetControl(requireNeutral);
        if (ready && wasMoving) enqueueStop(null);
    }

    private static final class Tx {
        final byte[] bytes;
        final boolean motion, stop;
        final Runnable done;
        int offset;
        Tx(String text, boolean movement, boolean zero, Runnable completion) {
            bytes = text.getBytes(StandardCharsets.US_ASCII); motion = movement; stop = zero; done = completion;
        }
    }
    private static void removePendingMotion() {
        Iterator<Tx> it = queue.iterator();
        while (it.hasNext()) if (it.next().motion) it.remove();
    }
    private static void enqueueMotion() {
        removePendingMotion();
        if (queue.size() >= MAX_QUEUE) { fail("send queue overflow"); return; }
        // Configuration/query bursts must not starve the 100 ms control lease.
        // Finish an in-progress frame, then put motion immediately after any stop.
        Tx stop = queue.peekFirst();
        if (stop != null && stop.stop) queue.removeFirst();
        queue.addFirst(new Tx(latestMotion, true, false, null));
        if (stop != null && stop.stop) queue.addFirst(stop);
        pump();
    }
    private static void enqueueStop(Runnable completion) {
        removePendingMotion();
        // Old stop/arming barriers must not re-enable motion after release.
        Iterator<Tx> it = queue.iterator();
        while (it.hasNext()) if (it.next().stop) it.remove();
        if (queue.size() >= MAX_QUEUE) { fail("send queue overflow"); return; }
        queue.addFirst(new Tx(ZERO, false, true, completion));
        pump();
    }
    private static void enqueue(Tx tx) {
        if (queue.size() >= MAX_QUEUE) { fail("send queue overflow"); return; }
        queue.addLast(tx);
        pump();
    }
    private static long nextWriteAt;
    private static void pump() {
        if (!ready || gatt == null || waitingWrite) return;
        long wait = nextWriteAt - android.os.SystemClock.uptimeMillis();
        if (wait > 0) { LOOP.removeCallbacks(PUMP); LOOP.postDelayed(PUMP, wait); return; }
        if (active != null && active.offset == active.bytes.length) {
            Runnable completion = active.done;
            active = null;
            if (completion != null) completion.run();
            if (!ready || active != null || waitingWrite) return;
        }
        if (active == null) active = queue.pollFirst();
        if (active == null) return;
        if (active.motion && (!heartbeat || !visible())) { active = null; pump(); return; }
        int end = Math.min(active.offset + ATT_PAYLOAD, active.bytes.length);
        byte[] chunk = Arrays.copyOfRange(active.bytes, active.offset, end);
        try {
            write.setWriteType(writeType);
            write.setValue(chunk);
            if (!gatt.writeCharacteristic(write)) { fail("GATT write rejected"); return; }
        } catch (RuntimeException e) { fail("GATT write denied"); return; }
        active.offset = end;
        nextWriteAt = android.os.SystemClock.uptimeMillis() + WRITE_GAP_MS;
        if (writeType == BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) {
            waitingWrite = true;
            final long session = generation, ticket = ++writeTicket;
            LOOP.postDelayed(() -> { if (current(session) && waitingWrite && writeTicket == ticket) fail("GATT write timeout"); }, WRITE_TIMEOUT_MS);
        } else {
            LOOP.removeCallbacks(PUMP);
            LOOP.postDelayed(PUMP, WRITE_GAP_MS);
        }
    }
    private static final Runnable PUMP = MiniBalanLink::pump;

    public static void stop() {
        // A legacy sender that slept across session takeover must never close this GATT.
        if (LEGACY_DISPATCH.get() != null) return;
        run(() -> { if (owned) { halt(); disconnectAfterStop(); } });
    }
    private static void disconnectAfterStop() {
        // Best effort zero is queued ahead of ordinary commands. The MCU watchdog
        // remains authoritative if radio/OS prevents delivery.
        final long session = generation;
        if (ready) {
            enqueueStop(() -> { if (current(session)) fail("user disconnect"); });
            LOOP.postDelayed(() -> { if (current(session)) fail("disconnect deadline"); }, 200);
        } else fail("user disconnect");
    }
    public static void leaveForLegacy() {
        run(() -> { if (owned) { halt(); closeGatt(); generation++; setOwned(false); } });
    }
    public static void destroy() {
        run(() -> { if (owned) { halt(); closeGatt(); generation++; setOwned(false); ui.clear(); } control.clear(); controlView.clear(); });
    }
    private static void fail(String reason) {
        Log.w(TAG, reason);
        closeGatt();
        final long session = ++generation;
        resetControl();
        emit(6, null, session);
    }
    private static void closeGatt() {
        ready = false; waitingWrite = false; writeTicket++; nextWriteAt = 0;
        queue.clear(); active = null; decoder.reset(); LOOP.removeCallbacks(PUMP);
        BluetoothGatt old = gatt;
        gatt = null; write = null; notify = null; cccd = null;
        if (old != null) {
            try { old.disconnect(); } catch (RuntimeException ignored) { }
            try { old.close(); } catch (RuntimeException ignored) { }
        }
    }
    private static boolean current(long session) { return owned && session == generation; }
    private static boolean current(long session, BluetoothGatt source) { return current(session) && source == gatt; }
    private static void emit(int what, String data, long session) {
        Handler target = ui.get();
        if (target == null) return;
        target.post(() -> {
            if (!current(session) || ui.get() != target || (what == 103 && !ready)) return;
            Message message = target.obtainMessage(what, data);
            message.arg1 = (int) session;
            message.arg2 = UI_MARKER;
            // Direct dispatch after the generation check avoids a second unguarded queue.
            try { UI_EVENT.set(message); target.dispatchMessage(message); }
            catch (RuntimeException e) {
                Log.e(TAG, "MiniBalan UI rejected validated event " + what, e);
                if (what == 3) run(() -> fail("ready UI failed"));
            } finally { UI_EVENT.remove(); message.recycle(); }
        });
    }

    /** Final UI-handler ingress. A marker alone is insufficient: only the current
     * guarded synchronous dispatch may enter the legacy MiniBalan presentation.
     * Old BLE dispatcher messages queued before takeover are discarded here.
     */
    public static boolean acceptUiEvent(Message message) {
        if (UI_EVENT.get() == message) {
            if (!owned || message.arg1 != (int) generation) return false;
            if (message.what == 3) return ready;
            if (message.what == 6) return !ready;
            return message.what == 103 && ready && message.obj instanceof String
                && ProtocolValidation.validTelemetry((String) message.obj);
        }
        if (message.arg2 == UI_MARKER || message.what == 103) return false;
        if (!owned) return true;
        switch (message.what) {
            case 3: case 4: case 5: case 6: case 11: case 12: case 13: case 14:
            case 15: case 16: case 21: case 78: case 86: case 87: case 92:
            case 101: case 104: return false;
            default: return true; // Includes the original UI ticker (20).
        }
    }

    private static void setOwned(boolean value) {
        LEGACY_GATE.lock();
        try { owned = value; } finally { LEGACY_GATE.unlock(); }
    }
    public static boolean beginLegacyDispatch() {
        if (owned) return false;
        LEGACY_DISPATCH.set(generation);
        if (owned) { LEGACY_DISPATCH.remove(); return false; }
        return true;
    }
    public static void endLegacyDispatch() { LEGACY_DISPATCH.remove(); }
    public static boolean legacyDispatchCurrent() {
        Long epoch = LEGACY_DISPATCH.get();
        return !owned && (epoch == null || epoch.longValue() == generation);
    }
    public static boolean allowStop() {
        return LEGACY_DISPATCH.get() == null || legacyDispatchCurrent();
    }
    /** Held only around the legacy GATT API operation, never around its sleep or
     * entire send loop. Ownership takeover uses the same lock: no check/use race.
     */
    public static boolean beginLegacyOperation() {
        LEGACY_GATE.lock();
        if (!legacyDispatchCurrent()) { LEGACY_GATE.unlock(); return false; }
        return true;
    }
    public static void endLegacyOperation() { LEGACY_GATE.unlock(); }

    private static String hex(byte[] bytes) {
        StringBuilder out = new StringBuilder();
        final char[] digits = "0123456789abcdef".toCharArray();
        for (int i = 0; i < Math.min(bytes.length, 129); i++) {
            int value = bytes[i] & 255;
            out.append(digits[value >>> 4]).append(digits[value & 15]);
        }
        return out.toString();
    }
    private static void run(Runnable action) {
        if (Looper.myLooper() == Looper.getMainLooper()) action.run(); else LOOP.post(action);
    }
    public static String displayName(BluetoothDevice device) {
        try { String value = device.getName(); return value == null || value.length() == 0 ? "Unnamed BLE device" : value; }
        catch (SecurityException e) { return "BLE device"; }
    }
    public static void unregister(Context registered, BroadcastReceiver receiver) {
        if (registered == null || receiver == null) return;
        try { registered.unregisterReceiver(receiver); }
        catch (IllegalArgumentException ignored) { Log.d(TAG, "receiver already unregistered"); }
    }
}
