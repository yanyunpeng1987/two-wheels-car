package com.mvtbot.link;

import android.app.Activity;
import android.os.Build;
import android.util.Log;
import android.widget.Toast;
import java.util.ArrayList;
import java.util.Locale;

/** Runtime permission gate for the MiniBalan Bluetooth button, never app startup. */
public final class ManualBlePermissions {
    private static final String TAG = "MVTBOT-Scan";
    private static final String SCAN = "android.permission.BLUETOOTH_SCAN";
    private static final String CONNECT = "android.permission.BLUETOOTH_CONNECT";
    private static final String FINE = "android.permission.ACCESS_FINE_LOCATION";

    private ManualBlePermissions() { }

    public static boolean ensure(Activity activity) {
        if (activity == null) return false;
        if (Build.VERSION.SDK_INT < 23) return true;
        try {
            // This app is a central, not an advertiser. Its manifest declares
            // SCAN neverForLocation, so Android 12+ needs no location permission.
            String[] required = Build.VERSION.SDK_INT >= 31 ? new String[] { SCAN, CONNECT } : new String[] { FINE };
            ArrayList<String> missing = new ArrayList<>();
            for (String permission : required) {
                if (activity.checkSelfPermission(permission) != 0) missing.add(permission);
            }
            if (missing.isEmpty()) {
                Log.i(TAG, "manual BLE permissions already granted; no request");
                return true;
            }
            Log.i(TAG, "manual BLE permission request: sdk=" + Build.VERSION.SDK_INT + " missingCount=" + missing.size());
            activity.requestPermissions(missing.toArray(new String[0]), 1);
            hint(activity, "授权后请再次点击蓝牙按钮", "After granting permission, tap Bluetooth again.");
            // requestPermissions can pause the Activity even when its result is
            // immediate. Never open a picker during this request transaction.
            return false;
        } catch (RuntimeException failure) {
            Log.w(TAG, "manual BLE permissions unavailable: exception=" + failure.getClass().getSimpleName());
            hint(activity, "请检查蓝牙权限后再次点击蓝牙按钮", "Check Bluetooth permissions, then tap Bluetooth again.");
            return false;
        }
    }

    private static void hint(Activity activity, String chinese, String english) {
        try {
            Toast.makeText(activity, "zh".equals(Locale.getDefault().getLanguage()) ? chinese : english, Toast.LENGTH_SHORT).show();
        } catch (RuntimeException failure) {
            Log.w(TAG, "manual BLE permission hint unavailable: exception=" + failure.getClass().getSimpleName());
        }
    }
}
