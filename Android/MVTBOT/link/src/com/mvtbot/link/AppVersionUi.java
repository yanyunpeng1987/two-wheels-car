package com.mvtbot.link;

import android.content.Context;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.res.Configuration;
import android.os.Build;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.PopupWindow;
import android.widget.RelativeLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.util.Locale;

/** Adds installed package metadata to the existing MiniBalan Contact popup. */
public final class AppVersionUi {
    private static final String TAG = "MVTBOT-UI";
    private static final String VERSION_TAG = "com.mvtbot.contact.installed-version";
    // Existing immutable v21 resource IDs; no added resources or application classes.
    private static final int COMPANY = 0x7f0800ef, WEBSITE = 0x7f0803f3;

    private AppVersionUi() { }

    public static void prepare(PopupWindow popup) {
        if (popup == null) return;
        try {
            View content = popup.getContentView();
            if (!(content instanceof LinearLayout)) return;
            LinearLayout root = (LinearLayout) content;
            View company = root.findViewById(COMPANY);
            View website = root.findViewById(WEBSITE);
            if (!(company instanceof TextView) || website == null
                    || !(website.getParent() instanceof RelativeLayout)) return;
            RelativeLayout center = (RelativeLayout) website.getParent();
            Context context = root.getContext();
            if (center.getParent() == root) {
                int index = root.indexOfChild(center);
                ViewGroup.LayoutParams weight = center.getLayoutParams();
                ScrollView scroll = new ScrollView(context);
                scroll.setFillViewport(true);
                // The existing center keeps its children, including the live URLSpan.
                // Its original 0-height/weight-4 slot moves to the scroll container;
                // the title and confirmation button remain outside that container.
                root.removeView(center);
                scroll.addView(center, new ScrollView.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
                root.addView(scroll, index, weight);
            } else if (!(center.getParent() instanceof ScrollView)) {
                return;
            }
            View existing = root.findViewWithTag(VERSION_TAG);
            TextView version;
            if (existing instanceof TextView) {
                version = (TextView) existing;
            } else {
                version = new TextView(context);
                version.setTag(VERSION_TAG);
                version.setTextSize(14);
                version.setTextColor(((TextView) company).getCurrentTextColor());
                version.setGravity(Gravity.CENTER);
                version.setSingleLine(false);
                RelativeLayout.LayoutParams row = new RelativeLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
                row.addRule(RelativeLayout.BELOW, WEBSITE);
                row.setMargins(dp(context, 16), dp(context, 12), dp(context, 16), dp(context, 12));
                center.addView(version, row);
            }
            version.setText(installedVersion(context));
            // Retain the established popup size unless a smaller screen cannot fit it.
            DisplayMetrics metrics = context.getResources().getDisplayMetrics();
            int inset = dp(context, 24);
            if (popup.getWidth() > 0) popup.setWidth(Math.min(popup.getWidth(), Math.max(1, metrics.widthPixels - inset)));
            if (popup.getHeight() > 0) popup.setHeight(Math.min(popup.getHeight(), Math.max(1, metrics.heightPixels - inset)));
        } catch (RuntimeException failure) {
            Log.w(TAG, "Contact version display unavailable: " + failure.getClass().getSimpleName());
        }
    }

    private static int dp(Context context, int value) {
        return Math.round(value * context.getResources().getDisplayMetrics().density);
    }

    @SuppressWarnings("deprecation")
    private static String installedVersion(Context context) {
        Configuration config = context.getResources().getConfiguration();
        Locale locale = Build.VERSION.SDK_INT >= 24 && !config.getLocales().isEmpty()
                ? config.getLocales().get(0) : config.locale;
        boolean chinese = locale != null && "zh".equals(locale.getLanguage());
        try {
            PackageInfo info = context.getPackageManager().getPackageInfo(context.getPackageName(), 0);
            long code = Build.VERSION.SDK_INT >= 28 ? info.getLongVersionCode() : info.versionCode;
            String name = info.versionName;
            if (name == null || name.trim().isEmpty()) name = chinese ? "未知" : "Unknown";
            return (chinese ? "版本：" : "Version: ") + name + " (" + code + ")";
        } catch (PackageManager.NameNotFoundException | RuntimeException failure) {
            Log.w(TAG, "Installed version lookup failed: " + failure.getClass().getSimpleName());
            return chinese ? "版本信息不可用" : "Version information unavailable";
        }
    }
}
