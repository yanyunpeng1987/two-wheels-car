package com.mvtbot.link;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothManager;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanFilter;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.Context;
import android.os.Handler;
import android.util.Log;
import java.io.FileNotFoundException;
import java.io.InputStream;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
import java.util.Properties;

/** Local diagnostic builds only. No address or enabled default is stored in source.
 * One scan attempt per process; all state and listener calls belong to the main looper.
 */
final class DebugAutoConnect {
    interface Listener {
        boolean canConnect();
        void connect(BluetoothDevice device);
    }
    private static final String TAG = "MVTBOT-Link";
    private static final String ASSET = "mvtbot-debug-autoconnect.properties";
    private static final long TIMEOUT_MS = 10000, FOREGROUND_SETTLE_MS = 500;
    private final Handler loop;
    private final Listener listener;
    private Context context;
    private boolean loaded, attempted, pending;
    private String target;
    private long token, deadlineAt;
    private BluetoothLeScanner scanner;
    private ScanCallback callback;

    DebugAutoConnect(Handler loop, Listener listener) {
        this.loop = loop;
        this.listener = listener;
    }

    void bind(Context appContext) {
        context = appContext;
        if (loaded) return;
        loaded = true;
        try (InputStream input = context.getAssets().open(ASSET)) {
            Properties properties = new Properties();
            properties.load(input);
            if (!"true".equals(properties.getProperty("enabled"))) return;
            String value = properties.getProperty("targetAddress", "").trim().toUpperCase(Locale.ROOT);
            if (properties.size() != 2 || !BluetoothAdapter.checkBluetoothAddress(value)) {
                Log.w(TAG, "debug auto-connect configuration rejected");
                return;
            }
            target = value;
            Log.i(TAG, "debug auto-connect enabled for one locally configured target");
        } catch (FileNotFoundException missing) {
            // Normal builds contain no diagnostic asset.
        } catch (Exception failure) {
            Log.w(TAG, "debug auto-connect configuration unavailable: " + failure.getClass().getSimpleName());
        }
    }

    void consider() {
        if (attempted || pending || target == null || context == null || !listener.canConnect()) return;
        pending = true;
        final long preparation = ++token;
        Log.i(TAG, "debug auto-connect waiting for 500ms stable foreground");
        loop.postDelayed(() -> {
            if (!pending || preparation != token) return;
            pending = false;
            if (attempted || !listener.canConnect()) return;
            beginScan();
        }, FOREGROUND_SETTLE_MS);
    }

    private void beginScan() {
        attempted = true;
        final long attempt = ++token;
        deadlineAt = android.os.SystemClock.uptimeMillis() + TIMEOUT_MS;
        try {
            BluetoothManager manager = (BluetoothManager) context.getSystemService(Context.BLUETOOTH_SERVICE);
            BluetoothAdapter adapter = manager == null ? null : manager.getAdapter();
            scanner = adapter == null || !adapter.isEnabled() ? null : adapter.getBluetoothLeScanner();
            if (scanner == null) {
                Log.w(TAG, "debug auto-connect scan unavailable; no retry in this process");
                return;
            }
            callback = new ScanCallback() {
                @Override public void onScanResult(int type, ScanResult result) {
                    loop.post(() -> found(attempt, result));
                }
                @Override public void onBatchScanResults(List<ScanResult> results) {
                    if (results != null) for (ScanResult result : results) loop.post(() -> found(attempt, result));
                }
                @Override public void onScanFailed(int code) {
                    loop.post(() -> {
                        if (!current(attempt)) return;
                        Log.w(TAG, "debug auto-connect scan failed: code=" + code);
                        stopScan();
                    });
                }
            };
            ScanFilter filter = new ScanFilter.Builder().setDeviceAddress(target).build();
            ScanSettings settings = new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build();
            scanner.startScan(Collections.singletonList(filter), settings, callback);
            Log.i(TAG, "debug auto-connect exact-target scan started; deadline=10000ms");
            loop.postDelayed(() -> {
                if (!current(attempt)) return;
                Log.w(TAG, "debug auto-connect scan timeout; no retry in this process");
                stopScan();
            }, TIMEOUT_MS);
        } catch (RuntimeException failure) {
            Log.w(TAG, "debug auto-connect scan denied: " + failure.getClass().getSimpleName());
            stopScan();
        }
    }

    private boolean current(long attempt) { return attempt == token && callback != null; }
    private void found(long attempt, ScanResult result) {
        if (!current(attempt) || result == null) return;
        if (android.os.SystemClock.uptimeMillis() >= deadlineAt) {
            Log.w(TAG, "debug auto-connect result arrived after scan deadline");
            stopScan();
            return;
        }
        if (!listener.canConnect()) { cancel("UI/session no longer available"); return; }
        try {
            BluetoothDevice device = result.getDevice();
            // Recheck even though the platform filter is exact. Never use name/RSSI as identity.
            if (device == null || !target.equalsIgnoreCase(device.getAddress())) return;
            stopScan();
            Log.i(TAG, "debug auto-connect target observed; entering normal guarded connection");
            listener.connect(device);
        } catch (RuntimeException failure) {
            Log.w(TAG, "debug auto-connect target unavailable: " + failure.getClass().getSimpleName());
            stopScan();
        }
    }

    void background() {
        if (pending) {
            pending = false;
            token++;
            Log.i(TAG, "debug auto-connect foreground wait cancelled; scan attempt remains available");
        } else if (callback != null) cancel("background");
    }
    void cancel(String reason) {
        attempted = true;
        if (callback != null) Log.i(TAG, "debug auto-connect scan cancelled: " + reason);
        stopScan();
    }
    private void stopScan() {
        BluetoothLeScanner previousScanner = scanner;
        ScanCallback previousCallback = callback;
        scanner = null;
        callback = null;
        pending = false;
        token++;
        if (previousScanner != null && previousCallback != null) {
            try { previousScanner.stopScan(previousCallback); }
            catch (RuntimeException failure) {
                Log.w(TAG, "debug auto-connect scan cleanup denied: " + failure.getClass().getSimpleName());
            }
        }
    }
}
