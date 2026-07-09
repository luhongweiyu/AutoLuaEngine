/**
 * 文件用途：封装亮屏、息屏、电池、旋转等设备控制相关命令。
 */
package com.autolua.engine;

import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Android 设备状态和系统级设备控制。
 *
 * 这些能力属于自动化引擎的基础设备层，第一版优先通过 root shell 实现：
 * 屏幕亮灭、唤醒、息屏、方向锁定和电量状态都不依赖无障碍服务。
 * 返回给 Native/HTTP 前会整理成简单的 key=value 文本，方便两边稳定解析。
 */
public final class DeviceControlBridge {
    private static final int KEYCODE_SLEEP = 223;
    private static final int KEYCODE_WAKEUP = 224;
    private static final int DEFAULT_TIMEOUT_MS = 2500;

    private static final Pattern SURFACE_ORIENTATION_PATTERN =
            Pattern.compile("SurfaceOrientation:\\s*(\\d+)");
    private static final Pattern ROTATION_NAME_PATTERN =
            Pattern.compile("m(?:Current)?Rotation=ROTATION_(\\d+)");
    private static final Pattern ROTATION_NUMBER_PATTERN =
            Pattern.compile("m(?:Current)?Rotation=(\\d+)");

    private DeviceControlBridge() {
    }

    public static RootCommandResult screenState() {
        RootCommandResult commandResult = RootShellBridge.exec(
                "(dumpsys power 2>/dev/null || true); "
                        + "printf '\\n__AEL_WINDOW__\\n'; "
                        + "(dumpsys window 2>/dev/null || true)",
                DEFAULT_TIMEOUT_MS
        );
        if (!commandResult.success) {
            return commandResult;
        }

        String text = commandResult.stdout;
        String wakefulness = firstValueAfter(text, "mWakefulness=", "Wakefulness=");
        String displayState = findDisplayState(text);
        boolean interactive = parseBoolean(firstValueAfter(text, "mInteractive="), isAwake(wakefulness));
        boolean screenOn = isScreenOn(displayState, interactive);
        boolean locked = hasTrueFlag(
                text,
                "mShowingLockscreen",
                "isKeyguardShowing",
                "keyguardShowing",
                "mKeyguardShowing",
                "isStatusBarKeyguard"
        );

        StringBuilder builder = new StringBuilder();
        appendLine(builder, "interactive", String.valueOf(interactive));
        appendLine(builder, "screenOn", String.valueOf(screenOn));
        appendLine(builder, "locked", String.valueOf(locked));
        appendLine(builder, "wakefulness", wakefulness);
        appendLine(builder, "displayState", displayState);
        return RootCommandResult.success(builder.toString());
    }

    public static RootCommandResult wake() {
        return RootShellBridge.exec("input keyevent " + KEYCODE_WAKEUP, DEFAULT_TIMEOUT_MS);
    }

    public static RootCommandResult sleep() {
        return RootShellBridge.exec("input keyevent " + KEYCODE_SLEEP, DEFAULT_TIMEOUT_MS);
    }

    public static RootCommandResult battery() {
        RootCommandResult commandResult = RootShellBridge.exec(
                "dumpsys battery 2>/dev/null",
                DEFAULT_TIMEOUT_MS
        );
        if (!commandResult.success) {
            return commandResult;
        }
        if (commandResult.stdout.trim().isEmpty()) {
            return RootCommandResult.failure("battery dumpsys output is empty");
        }

        String text = commandResult.stdout;
        int level = parseInt(findColonValue(text, "level"), -1);
        int scale = parseInt(findColonValue(text, "scale"), 100);
        int percent = level >= 0 && scale > 0 ? Math.round(level * 100f / scale) : -1;
        int statusCode = parseInt(findColonValue(text, "status"), 1);
        int healthCode = parseInt(findColonValue(text, "health"), 1);
        int voltageMv = parseInt(findColonValue(text, "voltage"), -1);
        int temperatureRaw = parseInt(findColonValue(text, "temperature"), Integer.MIN_VALUE);
        boolean acPowered = parseBoolean(findColonValue(text, "AC powered"), false);
        boolean usbPowered = parseBoolean(findColonValue(text, "USB powered"), false);
        boolean wirelessPowered = parseBoolean(findColonValue(text, "Wireless powered"), false);
        boolean present = parseBoolean(findColonValue(text, "present"), false);

        StringBuilder builder = new StringBuilder();
        appendLine(builder, "level", numberOrEmpty(level));
        appendLine(builder, "scale", numberOrEmpty(scale));
        appendLine(builder, "percent", numberOrEmpty(percent));
        appendLine(builder, "statusCode", String.valueOf(statusCode));
        appendLine(builder, "status", batteryStatusName(statusCode));
        appendLine(builder, "healthCode", String.valueOf(healthCode));
        appendLine(builder, "health", batteryHealthName(healthCode));
        appendLine(builder, "present", String.valueOf(present));
        appendLine(builder, "acPowered", String.valueOf(acPowered));
        appendLine(builder, "usbPowered", String.valueOf(usbPowered));
        appendLine(builder, "wirelessPowered", String.valueOf(wirelessPowered));
        appendLine(builder, "plugged", batteryPluggedName(acPowered, usbPowered, wirelessPowered));
        appendLine(builder, "voltageMv", numberOrEmpty(voltageMv));
        if (temperatureRaw != Integer.MIN_VALUE) {
            appendLine(builder, "temperatureC", String.format(Locale.US, "%.1f", temperatureRaw / 10.0f));
        }
        appendLine(builder, "technology", findColonValue(text, "technology"));
        return RootCommandResult.success(builder.toString());
    }

    public static RootCommandResult rotation() {
        RootCommandResult commandResult = RootShellBridge.exec(
                "printf 'accelerometer_rotation='; "
                        + "(settings get system accelerometer_rotation 2>/dev/null || printf 'null\\n'); "
                        + "printf 'user_rotation='; "
                        + "(settings get system user_rotation 2>/dev/null || printf 'null\\n'); "
                        + "printf '__AEL_DUMPSYS__\\n'; "
                        + "(dumpsys input 2>/dev/null || true); "
                        + "printf '\\n__AEL_WINDOW__\\n'; "
                        + "(dumpsys window 2>/dev/null || true)",
                DEFAULT_TIMEOUT_MS
        );
        if (!commandResult.success) {
            return commandResult;
        }

        String text = commandResult.stdout;
        int autoRotateValue = parseInt(firstValueAfter(text, "accelerometer_rotation="), 0);
        int userRotation = normalizeRotation(parseInt(firstValueAfter(text, "user_rotation="), 0));
        int currentRotation = findCurrentRotation(text);
        if (currentRotation < 0) {
            currentRotation = userRotation;
        }

        boolean autoRotate = autoRotateValue == 1;
        StringBuilder builder = new StringBuilder();
        appendLine(builder, "autoRotate", String.valueOf(autoRotate));
        appendLine(builder, "locked", String.valueOf(!autoRotate));
        appendLine(builder, "userRotation", String.valueOf(userRotation));
        appendLine(builder, "currentRotation", String.valueOf(currentRotation));
        appendLine(builder, "degrees", String.valueOf(currentRotation * 90));
        return RootCommandResult.success(builder.toString());
    }

    public static RootCommandResult setRotation(int rotation, boolean locked) {
        int normalizedRotation = normalizeRotation(rotation);
        if (normalizedRotation < 0) {
            return RootCommandResult.failure("rotation must be 0, 1, 2, 3, 90, 180 or 270");
        }

        String command;
        if (locked) {
            command = "settings put system accelerometer_rotation 0"
                    + " && settings put system user_rotation " + normalizedRotation
                    + " && (cmd window set-user-rotation lock " + normalizedRotation + " >/dev/null 2>&1 || true)";
        } else {
            command = "settings put system accelerometer_rotation 1"
                    + " && (cmd window set-user-rotation free >/dev/null 2>&1 || true)";
        }

        RootCommandResult commandResult = RootShellBridge.exec(command, DEFAULT_TIMEOUT_MS);
        if (!commandResult.success) {
            return commandResult;
        }
        return RootCommandResult.success("ok=true\n");
    }

    private static String firstValueAfter(String text, String... prefixes) {
        if (text == null || text.isEmpty()) {
            return "";
        }

        String[] lines = text.split("\\r?\\n");
        for (String line : lines) {
            String trimmed = line.trim();
            for (String prefix : prefixes) {
                int index = trimmed.indexOf(prefix);
                if (index >= 0) {
                    return takeFirstToken(trimmed.substring(index + prefix.length()));
                }
            }
        }
        return "";
    }

    private static String findColonValue(String text, String key) {
        if (text == null || text.isEmpty()) {
            return "";
        }

        String lowerKey = key.toLowerCase(Locale.US);
        String[] lines = text.split("\\r?\\n");
        for (String line : lines) {
            String trimmed = line.trim();
            int separator = trimmed.indexOf(':');
            if (separator <= 0) {
                continue;
            }

            String name = trimmed.substring(0, separator).trim().toLowerCase(Locale.US);
            if (lowerKey.equals(name)) {
                return trimmed.substring(separator + 1).trim();
            }
        }
        return "";
    }

    private static String findDisplayState(String text) {
        String displayState = firstValueAfter(text, "Display Power: state=", "mScreenState=");
        if (!displayState.isEmpty()) {
            return displayState;
        }
        return firstValueAfter(text, "state=");
    }

    private static boolean hasTrueFlag(String text, String... names) {
        if (text == null || text.isEmpty()) {
            return false;
        }

        String compact = text.replace(" ", "").toLowerCase(Locale.US);
        for (String name : names) {
            String flag = (name + "=true").toLowerCase(Locale.US);
            if (compact.contains(flag)) {
                return true;
            }
        }
        return false;
    }

    private static int findCurrentRotation(String text) {
        int value = firstMatchedRotation(SURFACE_ORIENTATION_PATTERN, text);
        if (value >= 0) {
            return normalizeRotation(value);
        }

        value = firstMatchedRotation(ROTATION_NAME_PATTERN, text);
        if (value >= 0) {
            return normalizeRotation(value);
        }

        value = firstMatchedRotation(ROTATION_NUMBER_PATTERN, text);
        return normalizeRotation(value);
    }

    private static int firstMatchedRotation(Pattern pattern, String text) {
        if (text == null || text.isEmpty()) {
            return -1;
        }

        Matcher matcher = pattern.matcher(text);
        if (!matcher.find()) {
            return -1;
        }

        return parseInt(matcher.group(1), -1);
    }

    private static boolean parseBoolean(String value, boolean defaultValue) {
        if (value == null || value.trim().isEmpty()) {
            return defaultValue;
        }

        String normalized = value.trim().toLowerCase(Locale.US);
        if ("true".equals(normalized) || "1".equals(normalized) || "yes".equals(normalized)) {
            return true;
        }
        if ("false".equals(normalized) || "0".equals(normalized) || "no".equals(normalized)) {
            return false;
        }
        return defaultValue;
    }

    private static int parseInt(String value, int defaultValue) {
        if (value == null || value.trim().isEmpty() || "null".equals(value.trim())) {
            return defaultValue;
        }

        try {
            return Integer.parseInt(value.trim());
        } catch (NumberFormatException ignored) {
            return defaultValue;
        }
    }

    private static int normalizeRotation(int rotation) {
        if (rotation >= 0 && rotation <= 3) {
            return rotation;
        }
        if (rotation == 90) {
            return 1;
        }
        if (rotation == 180) {
            return 2;
        }
        if (rotation == 270) {
            return 3;
        }
        return -1;
    }

    private static boolean isAwake(String wakefulness) {
        String value = wakefulness == null ? "" : wakefulness.toLowerCase(Locale.US);
        return value.contains("awake");
    }

    private static boolean isScreenOn(String displayState, boolean interactive) {
        if (displayState == null || displayState.isEmpty()) {
            return interactive;
        }

        String value = displayState.toUpperCase(Locale.US);
        return !value.contains("OFF");
    }

    private static String takeFirstToken(String value) {
        if (value == null) {
            return "";
        }

        String clean = value.trim();
        int end = clean.length();
        for (int i = 0; i < clean.length(); i++) {
            char c = clean.charAt(i);
            if (Character.isWhitespace(c) || c == ',' || c == ')') {
                end = i;
                break;
            }
        }
        return clean.substring(0, end);
    }

    private static String numberOrEmpty(int value) {
        return value < 0 ? "" : String.valueOf(value);
    }

    private static String batteryStatusName(int statusCode) {
        switch (statusCode) {
            case 2:
                return "charging";
            case 3:
                return "discharging";
            case 4:
                return "not_charging";
            case 5:
                return "full";
            case 1:
            default:
                return "unknown";
        }
    }

    private static String batteryHealthName(int healthCode) {
        switch (healthCode) {
            case 2:
                return "good";
            case 3:
                return "overheat";
            case 4:
                return "dead";
            case 5:
                return "over_voltage";
            case 6:
                return "unspecified_failure";
            case 7:
                return "cold";
            case 1:
            default:
                return "unknown";
        }
    }

    private static String batteryPluggedName(boolean acPowered, boolean usbPowered, boolean wirelessPowered) {
        if (acPowered) {
            return "ac";
        }
        if (usbPowered) {
            return "usb";
        }
        if (wirelessPowered) {
            return "wireless";
        }
        return "none";
    }

    private static void appendLine(StringBuilder builder, String key, String value) {
        if (value == null || value.isEmpty()) {
            return;
        }

        builder.append(key)
                .append('=')
                .append(value)
                .append('\n');
    }
}
