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
    private static final UUID DIS = uuid("180a");
    private static final UUID[] PROFILE_UUIDS = { uuid("2a29"), uuid("2a24"), uuid("2a26") };
    private static final String[] PROFILE_VALUES = { "QUALCOMM", "HC-05D", "1.1.4" };
    private static final String ZERO = "CMD|3|0|0|$", VOLTAGE_QUERY = "CMD|7|$";
    private static final int MAX_QUEUE = 24, ATT_PAYLOAD = 20;
    private static final long CONNECT_TIMEOUT_MS = 10000, WRITE_TIMEOUT_MS = 750, WRITE_GAP_MS = 30, HEARTBEAT_MS = 100, REARM_ZERO_GAP_MS = 100;
    private static final long HEALTH_INTERVAL_MS = 1000, HEALTH_QUERY_TIMEOUT_MS = 1500, HEALTH_MAX_AGE_MS = 2500;
    private static final Handler LOOP = new Handler(Looper.getMainLooper());
    private static final ReentrantLock LEGACY_GATE = new ReentrantLock();
    private static final ThreadLocal<Long> LEGACY_DISPATCH = new ThreadLocal<>();
    private static final ThreadLocal<Message> UI_EVENT = new ThreadLocal<>();
    private static final int UI_MARKER = 0x4d56544c;
    // ready is the internal GATT-write gate; operational is exposed to controls/UI.
    private static volatile boolean owned, ready, operational;
    private static boolean closing;
    private static int priorityRequests;
    private static boolean startupArming;
    private static long startupToken;
    private static volatile long generation;
    private static volatile String name = "";
    private static Context context;
    private static WeakReference<Handler> ui = new WeakReference<>(null);
    private static WeakReference<ControlReset> control = new WeakReference<>(null);
    private static WeakReference<View> controlView = new WeakReference<>(null);
    private static final DebugAutoConnect DEBUG_AUTO = new DebugAutoConnect(LOOP, new DebugAutoConnect.Listener() {
        public boolean canConnect() { return owned && foreground && ui.get() != null && gatt == null && !closing; }
        public void connect(BluetoothDevice device) { MiniBalanLink.connect(device); }
    });
    private static boolean foreground, heartbeat, armPending, gravityControl, gravityNeutralRequired = true;
    private static long gestureToken;
    private static String latestMotion = ZERO;
    private static BluetoothGatt gatt;
    private static BluetoothGattCharacteristic write, notify;
    private static BluetoothGattDescriptor cccd;
    private static int writeType;
    private static boolean waitingWrite;
    private static long writeTicket;
    private enum CompatState { NORMAL, IDENTIFYING, PROBING, VALIDATED }
    private static CompatState compatibility = CompatState.NORMAL;
    private static int profileIndex, observed13, adapted13;
    private static BluetoothGattCharacteristic profileRead;
    private static long profileReadTicket, healthToken, healthSubmittedNanos, lastHealthValidAt, healthFrameAt, healthReceiveAt;
    private static boolean healthPending, healthSubmitted, healthWriteDone;
    private static String healthVoltage;
    private static Tx healthTx;
    // Diagnostic context only. In no-response mode this describes the latest
    // submission, since Android does not supply a per-write transaction ID.
    private static int submittedWrites, submittedOffset = -1, submittedLength = -1, submittedWireLength = -1, submittedFrameLength = -1;
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
    // Separate assembly prevents health probes from erasing a fragmented PID/wave
    // frame in the presentation decoder. Reset exactly at the query submission.
    private static final FrameDecoder healthDecoder = new FrameDecoder(new FrameDecoder.Listener() {
        public void onFrame(String frame) {
            if (healthPending && healthSubmitted && healthVoltage == null && frame.startsWith("CMD|7|")) {
                String[] fields = frame.split("\\|");
                String number = fields[2].replace("$", "");
                int millivolts = Integer.parseInt(number); // FrameDecoder already checked int32 syntax.
                if (millivolts < 0 || millivolts > 40000) {
                    Log.w(TAG, "health voltage outside allowed 0..40000mV: " + millivolts);
                    return;
                }
                healthVoltage = frame.endsWith("|$") ? frame : frame.substring(0, frame.length() - 1) + "|$";
                healthFrameAt = healthReceiveAt;
                completeHealth();
            }
        }
        public void onRejected(String reason) { Log.w(TAG, "health frame rejected: " + reason); }
    });

    private MiniBalanLink() { }
    private static UUID uuid(String shortId) { return UUID.fromString("0000" + shortId + "-0000-1000-8000-00805f9b34fb"); }
    public static boolean isOwned() { return owned; }
    public static boolean isReady() { return owned && operational && !closing; }
    public static String deviceName() { return name; }

    public static void bind(Context appContext, Handler handler) {
        context = appContext.getApplicationContext();
        ui = new WeakReference<>(handler);
        DEBUG_AUTO.bind(context);
        DEBUG_AUTO.consider();
    }
    /** Claim the explicitly selected MiniBalan path even before a device is picked.
     * A still-connected legacy robot must not become this page's transport.
     */
    public static void selectMini() {
        if (owned) { DEBUG_AUTO.consider(); return; }
        setOwned(true);
        generation++;
        resetControl();
        DEBUG_AUTO.consider();
    }
    public static boolean connect(BluetoothDevice device) { return connect(context, ui.get(), device); }

    public static boolean connect(Context appContext, Handler handler, BluetoothDevice device) {
        DEBUG_AUTO.cancel("explicit connection");
        if (appContext == null || handler == null || device == null) {
            Log.w(TAG, "connect rejected before GATT: contextPresent=" + (appContext != null)
                + " handlerPresent=" + (handler != null) + " devicePresent=" + (device != null)
                + " owned=" + owned + " writable=" + ready + " operational=" + operational);
            return false;
        }
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
            Log.i(TAG, "connectGatt request: session=" + session + " transport=LE autoConnect=false");
            gatt = device.connectGatt(context, false, callback(session), BluetoothDevice.TRANSPORT_LE);
            if (gatt == null) { fail("connectGatt returned null"); return; }
        } catch (RuntimeException e) { fail("connect denied: " + e.getClass().getSimpleName()); return; }
        LOOP.postDelayed(() -> { if (current(session) && !operational) fail("connection/setup timeout"); }, CONNECT_TIMEOUT_MS);
    }

    private static BluetoothGattCallback callback(final long session) {
        return new BluetoothGattCallback() {
            @Override public void onConnectionStateChange(BluetoothGatt source, int status, int state) {
                LOOP.post(() -> {
                    if (!current(session, source)) return;
                    Log.i(TAG, "GATT state: status=" + statusText(status) + " state=" + state + " session=" + session);
                    if (status != BluetoothGatt.GATT_SUCCESS || state == BluetoothProfile.STATE_DISCONNECTED) {
                        fail("GATT disconnected/status=" + status); return;
                    }
                    if (state == BluetoothProfile.STATE_CONNECTED) {
                        requestHighPriority(session, source);
                        try { if (!source.discoverServices()) fail("service discovery rejected"); }
                        catch (RuntimeException e) { fail("service discovery denied"); }
                    }
                });
            }
            @Override public void onServicesDiscovered(BluetoothGatt source, int status) {
                LOOP.post(() -> {
                    if (!current(session, source)) return;
                    if (status != BluetoothGatt.GATT_SUCCESS) { fail("service discovery failed: status=" + statusText(status)); return; }
                    try {
                        List<BluetoothGattService> services = source.getServices();
                        logGattDatabase(services);
                        Pair pair = select(services);
                        if (pair == null) { fail("missing or ambiguous FFE1 write/notify characteristics"); return; }
                        write = pair.write; notify = pair.notify;
                        writeType = (write.getProperties() & BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE) != 0
                            ? BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE : BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT;
                        Log.i(TAG, "GATT selected: service=" + write.getService().getUuid()
                            + " write=" + characteristicMetadata(write) + " notify=" + characteristicMetadata(notify)
                            + " sameCharacteristic=" + (write == notify) + " writeType=" + writeType);
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
                    Log.i(TAG, "GATT CCCD callback: uuid=" + descriptor.getUuid() + " status=" + statusText(status));
                    if (status != BluetoothGatt.GATT_SUCCESS) { fail("CCCD write failed: status=" + statusText(status)); return; }
                    ready = true;
                    decoder.reset();
                    resetControl();
                    Log.i(TAG, "GATT writable; validating initial zero before enabling controls");
                    enqueueStop(() -> initialWriteComplete(session, source));
                });
            }
            @Override public void onCharacteristicChanged(BluetoothGatt source, BluetoothGattCharacteristic characteristic) {
                byte[] bytes = characteristic.getValue();
                acceptNotification(source, characteristic, bytes == null ? null : bytes.clone(), session, System.nanoTime(), android.os.SystemClock.uptimeMillis());
            }
            @Override public void onCharacteristicChanged(BluetoothGatt source, BluetoothGattCharacteristic characteristic, byte[] value) {
                acceptNotification(source, characteristic, value == null ? null : value.clone(), session, System.nanoTime(), android.os.SystemClock.uptimeMillis());
            }
            @Override public void onCharacteristicRead(BluetoothGatt source, BluetoothGattCharacteristic characteristic, int status) {
                byte[] bytes = characteristic.getValue();
                acceptProfileRead(source, characteristic, bytes == null ? null : bytes.clone(), status, session);
            }
            @Override public void onCharacteristicRead(BluetoothGatt source, BluetoothGattCharacteristic characteristic, byte[] value, int status) {
                acceptProfileRead(source, characteristic, value == null ? null : value.clone(), status, session);
            }
            @Override public void onCharacteristicWrite(BluetoothGatt source, BluetoothGattCharacteristic characteristic, int status) {
                LOOP.post(() -> {
                    if (!current(session, source) || characteristic != write || !ready) return;
                    if (status == 13) observed13++;
                    if (status != BluetoothGatt.GATT_SUCCESS) {
                        if (mayIdentifyProfile(status)) {
                            beginProfileVerification();
                            return;
                        }
                        if (!mayAdaptVendorCompletion(status)) {
                            fail("characteristic write failed: status=" + statusText(status) + " " + writeMetadata());
                            return;
                        }
                        adapted13++;
                        if (adapted13 <= 3 || adapted13 % 10 == 0) {
                            Log.w(TAG, "profile-scoped vendor completion13 (not an MCU ACK): observed=" + observed13
                                + " adapted=" + adapted13 + " " + writeMetadata());
                        }
                    }
                    if (waitingWrite) {
                        waitingWrite = false;
                        writeTicket++;
                        if (healthPending && active == healthTx && active.offset == active.bytes.length) healthWriteDone = true;
                        completeHealth();
                        // nextWriteAt already enforces the minimum submission gap.
                        // A slow response must not incur another unconditional 30ms.
                        pump();
                    }
                });
            }
        };
    }

    private static void acceptNotification(BluetoothGatt source, BluetoothGattCharacteristic characteristic, byte[] value, long session, long receivedNanos, long receivedMillis) {
        LOOP.post(() -> {
            if (!current(session, source) || !ready || characteristic != notify || value == null) return;
            boolean present = operational;
            if (healthPending && healthSubmitted && receivedNanos >= healthSubmittedNanos) {
                healthReceiveAt = receivedMillis;
                healthDecoder.feed(value);
            }
            if (present && operational) decoder.feed(value);
        });
    }
    private static void initialWriteComplete(long session, BluetoothGatt source) {
        if (!current(session, source) || !ready || operational || closing) return;
        operational = true;
        emit(3, null, session);
        LOOP.postDelayed(() -> tick(session), HEARTBEAT_MS);
        // The module may renegotiate slower parameters after discovery. Make
        // at most one further request after operational; never assume acceptance.
        LOOP.postDelayed(() -> { if (current(session, source) && operational) requestHighPriority(session, source); }, 1000);
        Log.i(TAG, "MiniBalan operational; service=" + write.getService().getUuid()
            + " writeType=" + writeType + " compatibility=" + compatibility);
    }
    private static void requestHighPriority(long session, BluetoothGatt source) {
        if (!current(session, source) || closing || priorityRequests >= 2) return;
        priorityRequests++;
        try {
            boolean accepted = source.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH);
            Log.i(TAG, "connection priority HIGH requested: attempt=" + priorityRequests + " APIaccepted=" + accepted
                + " (negotiated parameters require platform callback evidence)");
        } catch (RuntimeException error) {
            Log.w(TAG, "connection priority request unavailable: attempt=" + priorityRequests + " exception=" + error.getClass().getSimpleName());
        }
    }

    private static final class Pair {
        final BluetoothGattCharacteristic write, notify;
        Pair(BluetoothGattCharacteristic w, BluetoothGattCharacteristic n) { write = w; notify = n; }
    }
    private static String statusText(int status) { return status + "(0x" + Integer.toHexString(status) + ")"; }
    private static String characteristicMetadata(BluetoothGattCharacteristic c) {
        return "{uuid=" + c.getUuid() + " instance=" + c.getInstanceId()
            + " props=0x" + Integer.toHexString(c.getProperties())
            + " permissions=0x" + Integer.toHexString(c.getPermissions()) + "}";
    }
    private static void logGattDatabase(List<BluetoothGattService> services) {
        Log.i(TAG, "GATT database: services=" + services.size());
        for (BluetoothGattService service : services) {
            Log.i(TAG, "GATT service: uuid=" + service.getUuid() + " instance=" + service.getInstanceId()
                + " type=" + service.getType() + " characteristics=" + service.getCharacteristics().size());
            for (BluetoothGattCharacteristic c : service.getCharacteristics()) {
                Log.i(TAG, "GATT characteristic: service=" + service.getUuid() + " " + characteristicMetadata(c)
                    + " descriptors=" + c.getDescriptors().size());
                for (BluetoothGattDescriptor d : c.getDescriptors()) {
                    Log.i(TAG, "GATT descriptor: characteristic=" + c.getUuid() + " uuid=" + d.getUuid()
                        + " permissions=0x" + Integer.toHexString(d.getPermissions()));
                }
            }
        }
    }
    private static String writeMetadata() {
        return "writeType=" + writeType + " props=" + (write == null ? "none" : "0x" + Integer.toHexString(write.getProperties()))
            + " characteristic=" + (write == null ? "none" : write.getUuid())
            + " latestSubmission=" + submittedWrites + " offset=" + submittedOffset + " len=" + submittedLength
            + " dataLen=" + submittedLength + " wireLen=" + submittedWireLength + " compatibility=" + compatibility
            + " frameLen=" + submittedFrameLength + " advancedOffset=" + (active == null ? -1 : active.offset)
            + " waitingResponse=" + waitingWrite + " queueDepth=" + queue.size();
    }
    private static boolean vendorGattProfile() {
        return writeType == BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT && write != null && write == notify
            && FFE1.equals(write.getUuid()) && write.getProperties() == 0x18 && write.getService() != null
            && FFE0.equals(write.getService().getUuid());
    }
    private static boolean mayIdentifyProfile(int status) {
        if (status != 13 || compatibility != CompatState.NORMAL || !waitingWrite || closing || operational || !vendorGattProfile()
            || submittedWrites != 1 || submittedOffset != 0 || submittedLength != ZERO.length()
            || submittedWireLength != ZERO.length() || active == null || !active.stop || active.motion
            || active.offset != active.bytes.length || active.bytes.length != ZERO.length()
            || !ZERO.equals(new String(active.bytes, StandardCharsets.US_ASCII))) return false;
        return true;
    }
    private static boolean mayAdaptVendorCompletion(int status) {
        if (status != 13 || !vendorGattProfile() || !waitingWrite || active == null
            || submittedLength < 2 || submittedLength > ATT_PAYLOAD || submittedLength != submittedWireLength
            || !ProtocolValidation.validCommand(new String(active.bytes, StandardCharsets.US_ASCII))) return false;
        return compatibility == CompatState.VALIDATED
            || (compatibility == CompatState.PROBING && active == healthTx && healthPending && healthSubmitted);
    }
    private static void beginProfileVerification() {
        Log.w(TAG, "initial zero13: checking DIS compatibility profile; zero is not retried");
        compatibility = CompatState.IDENTIFYING;
        waitingWrite = false;
        writeTicket++;
        active = null;
        queue.clear();
        profileIndex = 0;
        readNextProfileField();
    }
    private static void readNextProfileField() {
        if (!ready || closing || compatibility != CompatState.IDENTIFYING) return;
        if (profileIndex == PROFILE_UUIDS.length) {
            compatibility = CompatState.PROBING;
            Log.i(TAG, "DIS compatibility allowlist matched QUALCOMM/HC-05D/1.1.4 (not security authentication)");
            requestHealth();
            return;
        }
        BluetoothGattCharacteristic found = null;
        int services = 0;
        for (BluetoothGattService service : gatt.getServices()) {
            if (!DIS.equals(service.getUuid())) continue;
            services++;
            for (BluetoothGattCharacteristic c : service.getCharacteristics()) {
                if (!PROFILE_UUIDS[profileIndex].equals(c.getUuid())) continue;
                if (found != null || (c.getProperties() & BluetoothGattCharacteristic.PROPERTY_READ) == 0) {
                    fail("DIS compatibility field ambiguous/unreadable"); return;
                }
                found = c;
            }
        }
        if (services != 1 || found == null) { fail("DIS compatibility field missing"); return; }
        profileRead = found;
        final BluetoothGattCharacteristic expected = found;
        final long session = generation, ticket = ++profileReadTicket;
        try {
            if (!gatt.readCharacteristic(found)) { fail("DIS read rejected"); return; }
        } catch (RuntimeException error) { fail("DIS read denied: " + error.getClass().getSimpleName()); return; }
        LOOP.postDelayed(() -> {
            if (current(session) && compatibility == CompatState.IDENTIFYING && profileRead == expected && profileReadTicket == ticket)
                fail("DIS read timeout");
        }, WRITE_TIMEOUT_MS);
    }
    private static void acceptProfileRead(BluetoothGatt source, BluetoothGattCharacteristic c, byte[] value, int status, long session) {
        LOOP.post(() -> {
            if (!current(session, source) || compatibility != CompatState.IDENTIFYING || profileRead != c || closing) return;
            profileRead = null;
            profileReadTicket++;
            if (status != BluetoothGatt.GATT_SUCCESS) { fail("DIS read failed: status=" + statusText(status)); return; }
            if (value == null || !Arrays.equals(value, PROFILE_VALUES[profileIndex].getBytes(StandardCharsets.US_ASCII))) {
                fail("DIS compatibility profile mismatch: uuid=" + PROFILE_UUIDS[profileIndex]); return;
            }
            Log.i(TAG, "DIS profile field verified: uuid=" + PROFILE_UUIDS[profileIndex] + " expected=" + PROFILE_VALUES[profileIndex]);
            profileIndex++;
            readNextProfileField();
        });
    }
    private static void requestHealth() {
        if (!ready || closing || healthPending || (compatibility != CompatState.PROBING && compatibility != CompatState.VALIDATED)) return;
        if (queue.size() >= MAX_QUEUE) { fail("health queue overflow"); return; }
        healthPending = true;
        healthSubmitted = false;
        healthWriteDone = false;
        healthVoltage = null;
        healthFrameAt = 0;
        healthToken++;
        healthTx = new Tx(VOLTAGE_QUERY, false, false, null, true);
        // No partial frame preemption. Pending stop and latest movement retain
        // priority, followed by the one health query, then ordinary configuration.
        List<Tx> priority = new ArrayList<>();
        while (!queue.isEmpty() && (queue.peekFirst().stop || queue.peekFirst().motion)) priority.add(queue.removeFirst());
        queue.addFirst(healthTx);
        for (int i = priority.size() - 1; i >= 0; i--) queue.addFirst(priority.get(i));
        pump();
    }
    private static void healthSubmitted(long requestNanos) {
        healthDecoder.reset();
        healthSubmittedNanos = requestNanos;
        healthSubmitted = true;
        final long session = generation, token = healthToken;
        Log.i(TAG, "health CMD7 submitted: token=" + token + " state=" + compatibility);
        LOOP.postDelayed(() -> {
            if (current(session) && healthPending && healthToken == token) fail("health CMD7 round-trip timeout");
        }, HEALTH_QUERY_TIMEOUT_MS);
    }
    private static void completeHealth() {
        if (!ready || closing || !healthPending || !healthSubmitted || !healthWriteDone || healthVoltage == null) return;
        boolean initial = compatibility == CompatState.PROBING;
        if (!initial && compatibility != CompatState.VALIDATED) return;
        long age = android.os.SystemClock.uptimeMillis() - healthFrameAt;
        if (age >= HEALTH_MAX_AGE_MS) { fail("health voltage expired before write completion"); return; }
        String voltage = healthVoltage;
        healthPending = false;
        healthSubmitted = false;
        healthWriteDone = false;
        healthVoltage = null;
        healthTx = null;
        lastHealthValidAt = healthFrameAt;
        compatibility = CompatState.VALIDATED;
        Log.i(TAG, "health round-trip verified: observed13=" + observed13 + " adapted13=" + adapted13);
        if (initial) {
            startStartupNeutralBarrier(generation, gatt, voltage);
        }
        final long session = generation;
        LOOP.postDelayed(() -> checkHealth(session), Math.max(0, HEALTH_INTERVAL_MS - age));
        LOOP.postDelayed(() -> checkHealth(session), Math.max(0, HEALTH_MAX_AGE_MS - age));
    }
    private static void checkHealth(long session) {
        if (!current(session) || !operational || closing || compatibility != CompatState.VALIDATED) return;
        long age = android.os.SystemClock.uptimeMillis() - lastHealthValidAt;
        if (age >= HEALTH_MAX_AGE_MS) { fail("health voltage stale for 2500ms"); return; }
        if (age >= HEALTH_INTERVAL_MS && !healthPending) requestHealth();
    }
    private static boolean startupCurrent(long session, BluetoothGatt source, long token) {
        return current(session, source) && ready && !operational && !closing && startupArming
            && startupToken == token && compatibility == CompatState.VALIDATED;
    }
    private static void startStartupNeutralBarrier(long session, BluetoothGatt source, String voltage) {
        startupArming = true;
        final long token = ++startupToken;
        Log.i(TAG, "profile startup neutral refresh: zero1 then zero2 after >=100ms, before operational");
        enqueueStop(() -> {
            if (!startupCurrent(session, source, token)) return;
            LOOP.postDelayed(() -> {
                if (!startupCurrent(session, source, token)) return;
                enqueueStop(() -> {
                    if (!startupCurrent(session, source, token)) return;
                    if (android.os.SystemClock.uptimeMillis() - lastHealthValidAt >= HEALTH_MAX_AGE_MS) {
                        fail("health voltage expired during startup neutral refresh"); return;
                    }
                    startupArming = false;
                    // Only the zero lease is refreshed. lastHealthValidAt remains
                    // the notification timestamp from the actual voltage proof.
                    initialWriteComplete(session, source);
                    emit(103, voltage, session);
                    checkHealth(session);
                });
            }, REARM_ZERO_GAP_MS);
        });
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
        Message presentation = UI_EVENT.get();
        if (presentation != null && presentation.what == 103
            && (command.startsWith("CMD|1|") || command.startsWith("CMD|2|"))) {
            Log.i(TAG, "suppressed parameter write generated by telemetry presentation: command=" + command.charAt(4));
            return true;
        }
        final long session = generation;
        final int[] motion = ProtocolValidation.motion(command);
        if (motion != null) {
            if (motion[0] == 0 && motion[1] == 0) { release(); return true; }
            return false;
        }
        if (compatibility == CompatState.VALIDATED && ("CMD|7|$".equals(command) || "CMD|7$".equals(command))) {
            // The legacy page ticker requests this about once a second too.
            // One owner schedules/coalesces the actual query and freshness window.
            run(() -> checkHealth(session));
            return true;
        }
        run(() -> { if (current(session) && operational && !closing) enqueue(new Tx(command, false, false, null)); });
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
        run(() -> { foreground = value; if (!value) { DEBUG_AUTO.background(); halt(); } else DEBUG_AUTO.consider(); });
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
            if (!isReady() || !visible()) { resetControl(); return; }
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
        if (!current(session) || !operational || closing) return;
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
        return current(session) && operational && !closing && heartbeat && armPending && visible()
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
        final boolean motion, stop, health;
        final Runnable done;
        int offset;
        Tx(String text, boolean movement, boolean zero, Runnable completion) {
            this(text, movement, zero, completion, false);
        }
        Tx(String text, boolean movement, boolean zero, Runnable completion, boolean healthQuery) {
            bytes = text.getBytes(StandardCharsets.US_ASCII); motion = movement; stop = zero; done = completion; health = healthQuery;
        }
    }
    private static void removePendingMotion() {
        Iterator<Tx> it = queue.iterator();
        while (it.hasNext()) if (it.next().motion) it.remove();
    }
    private static void enqueueMotion() {
        boolean healthQueued = healthPending && healthTx != null && queue.contains(healthTx);
        boolean movementAlreadyAhead = false;
        if (healthQueued) {
            for (Tx queued : queue) {
                if (queued == healthTx) break;
                if (queued.motion) movementAlreadyAhead = true;
            }
        }
        removePendingMotion();
        if (queue.size() >= MAX_QUEUE) { fail("send queue overflow"); return; }
        if (healthQueued && !movementAlreadyAhead) {
            // Once the one already-queued motion has gone ahead of a health
            // query, newer 100ms updates replace the slot just AFTER that query.
            // This prevents permanent starvation on a slow response link.
            List<Tx> ordered = new ArrayList<>(queue);
            queue.clear();
            for (Tx queued : ordered) {
                queue.addLast(queued);
                if (queued == healthTx) queue.addLast(new Tx(latestMotion, true, false, null));
            }
            pump();
            return;
        }
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
        if (!ready || gatt == null || waitingWrite || compatibility == CompatState.IDENTIFYING) return;
        if (operational && compatibility == CompatState.VALIDATED
            && android.os.SystemClock.uptimeMillis() - lastHealthValidAt >= HEALTH_MAX_AGE_MS) {
            fail("health voltage stale before next queued write"); return;
        }
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
        submittedWrites++;
        submittedOffset = active.offset;
        submittedLength = end - active.offset;
        submittedWireLength = chunk.length;
        submittedFrameLength = active.bytes.length;
        long requestNanos = System.nanoTime();
        try {
            write.setWriteType(writeType);
            write.setValue(chunk);
            if (!gatt.writeCharacteristic(write)) { fail("GATT write rejected: " + writeMetadata()); return; }
        } catch (RuntimeException e) { fail("GATT write denied: exception=" + e.getClass().getSimpleName() + " " + writeMetadata()); return; }
        if (active.health && active.offset == 0 && healthPending && active == healthTx) healthSubmitted(requestNanos);
        active.offset = end;
        nextWriteAt = android.os.SystemClock.uptimeMillis() + WRITE_GAP_MS;
        // Initial setup also waits for Android's completion callback in WWR mode.
        // This retains write type 1 but prevents exposing a failed initial write.
        if (writeType == BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT || !operational) {
            waitingWrite = true;
            final long session = generation, ticket = ++writeTicket;
            LOOP.postDelayed(() -> { if (current(session) && waitingWrite && writeTicket == ticket) fail("GATT write timeout: " + writeMetadata()); }, WRITE_TIMEOUT_MS);
        } else {
            LOOP.removeCallbacks(PUMP);
            LOOP.postDelayed(PUMP, WRITE_GAP_MS);
        }
        if (submittedWrites == 1) Log.i(TAG, "GATT first write accepted: " + writeMetadata());
    }
    private static final Runnable PUMP = MiniBalanLink::pump;

    public static void stop() {
        // A legacy sender that slept across session takeover must never close this GATT.
        if (LEGACY_DISPATCH.get() != null) return;
        run(() -> { DEBUG_AUTO.cancel("user disconnect"); if (owned) { halt(); disconnectAfterStop(); } });
    }
    private static void disconnectAfterStop() {
        // Best effort zero is queued ahead of ordinary commands. The MCU watchdog
        // remains authoritative if radio/OS prevents delivery.
        final long session = generation;
        if (!operational) { finish("user cancelled setup", 6); return; }
        closing = true;
        operational = false;
        if (ready) {
            enqueueStop(() -> { if (current(session)) finish("user disconnect", 6); });
            LOOP.postDelayed(() -> { if (current(session)) finish("disconnect deadline", 6); }, 200);
        } else finish("user disconnect", 6);
    }
    public static void leaveForLegacy() {
        run(() -> { DEBUG_AUTO.cancel("left MiniBalan"); if (owned) { halt(); closeGatt(); generation++; setOwned(false); } });
    }
    public static void destroy() {
        run(() -> { DEBUG_AUTO.cancel("destroy"); if (owned) { halt(); closeGatt(); generation++; setOwned(false); ui.clear(); } control.clear(); controlView.clear(); });
    }
    private static void fail(String reason) {
        finish(reason, closing || operational ? 6 : 4);
    }
    private static void finish(String reason, int event) {
        Log.w(TAG, reason);
        closeGatt();
        final long session = ++generation;
        resetControl();
        emit(event, null, session);
    }
    private static void closeGatt() {
        ready = false; operational = false; closing = false; waitingWrite = false; writeTicket++; nextWriteAt = 0;
        priorityRequests = 0; startupArming = false; startupToken++;
        submittedWrites = 0; submittedOffset = -1; submittedLength = -1; submittedWireLength = -1; submittedFrameLength = -1;
        compatibility = CompatState.NORMAL;
        profileIndex = 0; profileRead = null; profileReadTicket++; observed13 = 0; adapted13 = 0;
        healthPending = false; healthSubmitted = false; healthWriteDone = false; healthVoltage = null;
        healthTx = null; healthToken++; healthSubmittedNanos = 0; lastHealthValidAt = 0; healthFrameAt = 0; healthReceiveAt = 0; healthDecoder.reset();
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
            if (!current(session) || ui.get() != target || (what == 103 && !operational)) return;
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
            if (message.what == 3) return operational && !closing;
            if (message.what == 4) return !ready && !operational;
            if (message.what == 6) return !operational;
            return message.what == 103 && operational && message.obj instanceof String
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
