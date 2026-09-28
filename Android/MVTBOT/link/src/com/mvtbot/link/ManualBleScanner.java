package com.mvtbot.link;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanRecord;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import android.view.View;
import android.widget.PopupWindow;
import android.widget.TextView;
import java.lang.ref.WeakReference;
import java.util.ArrayDeque;
import java.util.List;
import java.util.Locale;

/** MiniBalan's user-triggered picker scan. No remembered target or automatic retry.
 * The smali bridge owns the existing picker UI; this class owns every scan lifetime.
 */
public final class ManualBleScanner {
    private static final String TAG = "MVTBOT-Scan";
    private static final long DURATION_MS = 10000, MIN_GAP_MS = 6500, WINDOW_MS = 30000;
    private static final int MAX_STARTS = 4, SEARCHING = 0x7f0f01fe, FINISHED = 0x7f0f01fa;
    private static final Handler LOOP = new Handler(Looper.getMainLooper());
    private static final ArrayDeque<Long> starts = new ArrayDeque<>();
    private static long serial, nextStartAt;
    private static Session active;

    private static final class Session {
        final long id, startedAt, deadline;
        final WeakReference<PopupWindow> owner;
        BluetoothAdapter.LeScanCallback sink;
        TextView title;
        View progress;
        BluetoothLeScanner scanner;
        ScanCallback callback;
        Runnable timeout;
        int results;
        Session(PopupWindow owner, BluetoothAdapter.LeScanCallback sink, TextView title, View progress) {
            id = ++serial;
            startedAt = SystemClock.uptimeMillis();
            deadline = startedAt + DURATION_MS;
            this.owner = new WeakReference<>(owner);
            this.sink = sink;
            this.title = title;
            this.progress = progress;
        }
    }

    private ManualBleScanner() { }

    public static void start(PopupWindow owner, BluetoothAdapter adapter,
            BluetoothAdapter.LeScanCallback sink, TextView title, View progress) {
        run(() -> begin(owner, adapter, sink, title, progress));
    }

    private static void begin(PopupWindow owner, BluetoothAdapter adapter,
            BluetoothAdapter.LeScanCallback sink, TextView title, View progress) {
        if (owner == null || !owner.isShowing() || sink == null) return;
        if (active != null && active.owner.get() == owner) return; // repeated show is idempotent
        cancel("replaced picker");
        long now = SystemClock.uptimeMillis();
        while (!starts.isEmpty() && now - starts.peekFirst() >= WINDOW_MS) starts.removeFirst();
        long allowedAt = nextStartAt;
        if (starts.size() >= MAX_STARTS) allowedAt = Math.max(allowedAt, starts.peekFirst() + WINDOW_MS);
        if (now < allowedAt) {
            show(title, progress, waitMessage(allowedAt - now), false);
            Log.w(TAG, "scan throttled: waitMs=" + (allowedAt - now) + " recentStarts=" + starts.size());
            return;
        }
        final Session session = new Session(owner, sink, title, progress);
        active = session;
        try {
            session.scanner = adapter == null || !adapter.isEnabled() ? null : adapter.getBluetoothLeScanner();
            if (session.scanner == null) {
                finish(session, "unavailable", message("蓝牙未就绪，请开启蓝牙后重新搜索", "Bluetooth unavailable. Enable it and search again."));
                return;
            }
            session.callback = new ScanCallback() {
                @Override public void onScanResult(int type, ScanResult result) {
                    LOOP.post(() -> found(session, result));
                }
                @Override public void onBatchScanResults(List<ScanResult> results) {
                    if (results != null) for (ScanResult result : results) LOOP.post(() -> found(session, result));
                }
                @Override public void onScanFailed(int code) {
                    LOOP.post(() -> {
                        if (active != session) return;
                        long wait = code == 6 ? WINDOW_MS : MIN_GAP_MS;
                        nextStartAt = Math.max(nextStartAt, SystemClock.uptimeMillis() + wait);
                        Log.w(TAG, "scan failed: id=" + session.id + " code=" + code + " cooldownMs=" + wait);
                        finish(session, "failure=" + code, message("扫描失败（" + code + "），", "Scan failed (" + code + "). ")
                                + waitMessage(nextStartAt - SystemClock.uptimeMillis()));
                    });
                }
            };
            session.timeout = () -> { if (active == session) finish(session, "timeout", null); };
            starts.addLast(now);
            nextStartAt = now + MIN_GAP_MS;
            show(title, progress, null, true);
            session.scanner.startScan(null, new ScanSettings.Builder()
                    .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(), session.callback);
            Log.i(TAG, "scan start: id=" + session.id + " recentStarts=" + starts.size() + " deadlineMs=" + DURATION_MS);
            LOOP.postDelayed(session.timeout, DURATION_MS);
        } catch (RuntimeException failure) {
            Log.w(TAG, "scan start failed: id=" + session.id + " exception=" + failure.getClass().getSimpleName());
            finish(session, "exception", message("扫描不可用，请检查蓝牙权限后重新搜索", "Scan unavailable. Check Bluetooth permissions and search again."));
        }
    }

    private static void found(Session session, ScanResult result) {
        if (active != session || result == null) return;
        PopupWindow owner = session.owner.get();
        if (owner == null || !owner.isShowing()) { finish(session, "picker hidden", null); return; }
        if (SystemClock.uptimeMillis() >= session.deadline) { finish(session, "result after deadline", null); return; }
        try {
            if (result.getDevice() == null) return;
            if (++session.results == 1) Log.i(TAG, "scan first result: id=" + session.id
                    + " delayMs=" + (SystemClock.uptimeMillis() - session.startedAt));
            ScanRecord record = result.getScanRecord();
            session.sink.onLeScan(result.getDevice(), result.getRssi(), record == null ? new byte[0] : record.getBytes());
        } catch (RuntimeException failure) {
            Log.w(TAG, "scan result rejected: id=" + session.id + " exception=" + failure.getClass().getSimpleName());
            finish(session, "result error", message("设备列表不可用，请重新搜索", "Device list unavailable. Search again."));
        }
    }

    /** Stop only this picker; a late dismiss from an old picker cannot stop a new scan. */
    public static void stop(PopupWindow owner) {
        run(() -> { if (active != null && active.owner.get() == owner) finish(active, "picker stop", null); });
    }

    /** Connection and Activity lifecycle hooks. Synchronous when already on main. */
    public static void cancel(String reason) {
        run(() -> { if (active != null) finish(active, reason, null); });
    }

    private static void finish(Session session, String reason, String error) {
        if (active != session) return;
        active = null; // invalidate before stopScan, which may race a platform callback
        if (session.timeout != null) LOOP.removeCallbacks(session.timeout);
        BluetoothLeScanner scanner = session.scanner;
        ScanCallback callback = session.callback;
        session.scanner = null;
        session.callback = null;
        session.timeout = null;
        session.sink = null; // stale platform callbacks must not retain an Activity through the picker
        show(session.title, session.progress, error, false);
        session.title = null;
        session.progress = null;
        session.owner.clear();
        if (scanner != null && callback != null) {
            try { scanner.stopScan(callback); }
            catch (RuntimeException failure) {
                Log.w(TAG, "scan stop failed: id=" + session.id + " exception=" + failure.getClass().getSimpleName());
            }
        }
        Log.i(TAG, "scan stop: id=" + session.id + " reason=" + reason + " elapsedMs="
                + (SystemClock.uptimeMillis() - session.startedAt) + " results=" + session.results);
    }

    private static void show(TextView title, View progress, String error, boolean scanning) {
        if (title != null) { if (error == null) title.setText(scanning ? SEARCHING : FINISHED); else title.setText(error); }
        if (progress != null) progress.setVisibility(scanning ? View.VISIBLE : View.GONE);
    }
    private static String message(String chinese, String english) {
        return "zh".equals(Locale.getDefault().getLanguage()) ? chinese : english;
    }
    private static String waitMessage(long millis) {
        long seconds = Math.max(1, (millis + 999) / 1000);
        return message("请等待 " + seconds + " 秒后点重新搜索", "Wait " + seconds + "s, then search again.");
    }
    private static void run(Runnable action) {
        if (Looper.myLooper() == Looper.getMainLooper()) action.run(); else LOOP.post(action);
    }
}
