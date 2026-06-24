package com.autolua.engine;

import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;

import java.util.regex.Pattern;

/**
 * Android 应用控制桥。
 *
 * 启动应用优先走 root `monkey`，失败后回退普通 launcher Intent；
 * 停止其他应用需要 root，因此只走 `am force-stop`。
 */
public final class AppControlBridge {
    private static final int DEFAULT_TIMEOUT_MS = 2500;
    private static final Pattern PACKAGE_NAME_PATTERN =
            Pattern.compile("[A-Za-z][A-Za-z0-9_]*(\\.[A-Za-z][A-Za-z0-9_]*)+");

    private AppControlBridge() {
    }

    public static boolean isInstalled(Context context, String packageName) {
        if (!isValidPackageName(packageName)) {
            return false;
        }

        try {
            context.getPackageManager().getPackageInfo(packageName, 0);
            return true;
        } catch (PackageManager.NameNotFoundException exception) {
            return false;
        }
    }

    public static boolean open(Context context, String packageName, boolean rootModeEnabled) {
        if (!isValidPackageName(packageName)) {
            return false;
        }

        if (rootModeEnabled && RootShellBridge.isRootAvailable()) {
            RootCommandResult result = RootShellBridge.exec(
                    "monkey -p " + packageName + " -c android.intent.category.LAUNCHER 1",
                    DEFAULT_TIMEOUT_MS
            );
            if (result.success) {
                return true;
            }
        }

        return openByLauncherIntent(context, packageName);
    }

    public static boolean stop(String packageName) {
        if (!isValidPackageName(packageName)) {
            return false;
        }

        RootCommandResult result = RootShellBridge.exec(
                "am force-stop " + packageName,
                DEFAULT_TIMEOUT_MS
        );
        return result.success;
    }

    private static boolean openByLauncherIntent(Context context, String packageName) {
        Intent intent = context.getPackageManager().getLaunchIntentForPackage(packageName);
        if (intent == null) {
            return false;
        }

        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        try {
            context.startActivity(intent);
            return true;
        } catch (RuntimeException exception) {
            return false;
        }
    }

    private static boolean isValidPackageName(String packageName) {
        return packageName != null && PACKAGE_NAME_PATTERN.matcher(packageName).matches();
    }
}
