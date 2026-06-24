package com.autolua.engine;

import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;

import java.util.regex.Pattern;

/**
 * Android 应用控制桥。
 *
 * 启动应用优先走 root `monkey`，失败后回退普通 launcher Intent；
 * 停止、清理数据和授权管理都属于系统级操作，需要 root。
 */
public final class AppControlBridge {
    private static final int DEFAULT_TIMEOUT_MS = 2500;
    private static final Pattern PACKAGE_NAME_PATTERN =
            Pattern.compile("[A-Za-z][A-Za-z0-9_]*(\\.[A-Za-z][A-Za-z0-9_]*)+");
    private static final Pattern PERMISSION_NAME_PATTERN =
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

    public static boolean clearData(String packageName) {
        if (!isValidPackageName(packageName)) {
            return false;
        }

        // `pm clear` 会删除目标应用本地数据，属于显式 root 能力，不跟随 Root 模式开关降级。
        RootCommandResult result = RootShellBridge.exec(
                "pm clear " + packageName,
                DEFAULT_TIMEOUT_MS
        );
        return result.success;
    }

    public static boolean grantPermission(String packageName, String permissionName) {
        return changePermission(packageName, permissionName, true);
    }

    public static boolean revokePermission(String packageName, String permissionName) {
        return changePermission(packageName, permissionName, false);
    }

    private static boolean changePermission(
            String packageName,
            String permissionName,
            boolean grant) {
        if (!isValidPackageName(packageName) || !isValidPermissionName(permissionName)) {
            return false;
        }

        // 包名和权限名只允许 Java/Android 常见标识符字符，避免 shell 命令被额外参数污染。
        String command = grant ? "pm grant " : "pm revoke ";
        RootCommandResult result = RootShellBridge.exec(
                command + packageName + " " + permissionName,
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

    private static boolean isValidPermissionName(String permissionName) {
        return permissionName != null && PERMISSION_NAME_PATTERN.matcher(permissionName).matches();
    }
}
