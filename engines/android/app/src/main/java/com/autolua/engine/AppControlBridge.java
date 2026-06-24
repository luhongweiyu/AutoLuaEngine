package com.autolua.engine;

import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;

import java.util.regex.Pattern;

/**
 * Android 应用控制桥。
 *
 * 启动应用优先走 root `monkey`，失败后回退普通 launcher Intent；
 * 停止、清理数据、授权管理和包管理都属于系统级操作，需要 root。
 */
public final class AppControlBridge {
    private static final int DEFAULT_TIMEOUT_MS = 2500;
    private static final int PACKAGE_OPERATION_TIMEOUT_MS = 30_000;
    private static final Pattern PACKAGE_NAME_PATTERN =
            Pattern.compile("[A-Za-z][A-Za-z0-9_]*(\\.[A-Za-z][A-Za-z0-9_]*)+");
    private static final Pattern PERMISSION_NAME_PATTERN =
            Pattern.compile("[A-Za-z][A-Za-z0-9_]*(\\.[A-Za-z][A-Za-z0-9_]*)+");
    private static final Pattern CLASS_NAME_PATTERN =
            Pattern.compile("[A-Za-z_][A-Za-z0-9_$]*(\\.[A-Za-z_][A-Za-z0-9_$]*)*");

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

    public static RootCommandResult current() {
        RootCommandResult result = RootShellBridge.exec(
                "dumpsys window 2>/dev/null "
                        + "| grep -m 1 'mCurrentFocus=' "
                        + "| sed -n 's/.* \\([^ ]*\\/[^ ]*\\)}.*/\\1/p'",
                DEFAULT_TIMEOUT_MS
        );
        if (!result.success) {
            return result;
        }
        if (!isValidComponentLine(result.stdout)) {
            return RootCommandResult.failure("current app is not available");
        }
        return result;
    }

    public static boolean install(String apkPath, boolean replace) {
        if (!isValidAbsolutePath(apkPath)) {
            return false;
        }

        // 安装 APK 可能触发包校验和 dex 优化，不能复用点击/强停类操作的短超时。
        String command = replace ? "pm install -r " : "pm install ";
        RootCommandResult result = RootShellBridge.exec(
                command + RootShellBridge.shellQuote(apkPath),
                PACKAGE_OPERATION_TIMEOUT_MS
        );
        return result.success;
    }

    public static boolean uninstall(String packageName, boolean keepData) {
        if (!isValidPackageName(packageName)) {
            return false;
        }

        String command = keepData ? "pm uninstall -k " : "pm uninstall ";
        RootCommandResult result = RootShellBridge.exec(
                command + packageName,
                PACKAGE_OPERATION_TIMEOUT_MS
        );
        return result.success;
    }

    public static boolean disable(String packageName) {
        return setEnabledState(packageName, false);
    }

    public static boolean enable(String packageName) {
        return setEnabledState(packageName, true);
    }

    public static boolean disableComponent(String componentName) {
        return setComponentEnabledState(componentName, false);
    }

    public static boolean enableComponent(String componentName) {
        return setComponentEnabledState(componentName, true);
    }

    private static boolean setEnabledState(String packageName, boolean enabled) {
        if (!isValidPackageName(packageName)) {
            return false;
        }

        String command = enabled ? "pm enable " : "pm disable-user --user 0 ";
        RootCommandResult result = RootShellBridge.exec(command + packageName, DEFAULT_TIMEOUT_MS);
        return result.success;
    }

    private static boolean setComponentEnabledState(String componentName, boolean enabled) {
        String normalized = normalizeComponentName(componentName);
        if (normalized.isEmpty()) {
            return false;
        }

        // 组件启停属于显式 root 能力。组件名通过严格校验后不再 shellQuote，保持 pm 原生命令格式。
        String command = enabled ? "pm enable " : "pm disable-user --user 0 ";
        RootCommandResult result = RootShellBridge.exec(command + normalized, DEFAULT_TIMEOUT_MS);
        return result.success;
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

    private static String normalizeComponentName(String componentName) {
        if (componentName == null) {
            return "";
        }

        String value = componentName.trim();
        int separator = value.indexOf('/');
        if (separator <= 0 || separator >= value.length() - 1) {
            return "";
        }

        String packageName = value.substring(0, separator);
        String className = value.substring(separator + 1);
        if (!isValidPackageName(packageName) || !isValidComponentClassName(packageName, className)) {
            return "";
        }

        return packageName + "/" + className;
    }

    private static boolean isValidComponentClassName(String packageName, String className) {
        if (className == null || className.isEmpty()) {
            return false;
        }
        if (className.charAt(0) == '.') {
            return className.length() > 1 && CLASS_NAME_PATTERN.matcher(className.substring(1)).matches();
        }
        if (className.startsWith(packageName + ".")) {
            return CLASS_NAME_PATTERN.matcher(className).matches();
        }
        return CLASS_NAME_PATTERN.matcher(className).matches();
    }

    private static boolean isValidComponentLine(String value) {
        if (value == null) {
            return false;
        }

        String component = value.trim();
        int separator = component.indexOf('/');
        return separator > 0
                && separator < component.length() - 1
                && isValidPackageName(component.substring(0, separator));
    }

    private static boolean isValidAbsolutePath(String path) {
        return path != null && path.startsWith("/") && !path.contains("\u0000");
    }
}
