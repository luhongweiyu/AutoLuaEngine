/**
 * 文件用途：封装屏幕尺寸、密度、亮度等显示相关 Root 命令。
 */
package com.autolua.engine;

import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Android 显示参数 root 桥。
 *
 * 这里封装 `wm size`、`wm density` 和亮度 settings，后续截图坐标、分辨率适配、
 * 多设备脚本调试都可以走这一层，不需要脚本直接拼 shell 命令。
 */
public final class DeviceDisplayBridge {
    private static final int DEFAULT_TIMEOUT_MS = 2500;
    private static final Pattern SIZE_PATTERN = Pattern.compile("(\\d+)x(\\d+)");
    private static final Pattern DENSITY_PATTERN = Pattern.compile("(\\d+)");
    private static final int MIN_DISPLAY_SIZE = 1;
    private static final int MAX_DISPLAY_SIZE = 10000;
    private static final int MIN_DENSITY = 72;
    private static final int MAX_DENSITY = 1000;
    private static final int MIN_BRIGHTNESS = 0;
    private static final int MAX_BRIGHTNESS = 255;

    private DeviceDisplayBridge() {
    }

    public static RootCommandResult info() {
        RootCommandResult commandResult = RootShellBridge.exec(
                "wm size; "
                        + "wm density; "
                        + "printf 'screen_brightness='; settings get system screen_brightness 2>/dev/null || printf 'null\\n'; "
                        + "printf 'screen_brightness_mode='; settings get system screen_brightness_mode 2>/dev/null || printf 'null\\n'",
                DEFAULT_TIMEOUT_MS
        );
        if (!commandResult.success) {
            return commandResult;
        }

        DisplayInfo displayInfo = parseDisplayInfo(commandResult.stdout);
        StringBuilder builder = new StringBuilder();
        appendLine(builder, "physicalWidth", displayInfo.physicalWidth);
        appendLine(builder, "physicalHeight", displayInfo.physicalHeight);
        appendLine(builder, "overrideWidth", displayInfo.overrideWidth);
        appendLine(builder, "overrideHeight", displayInfo.overrideHeight);
        appendLine(builder, "effectiveWidth", displayInfo.overrideWidth > 0
                ? displayInfo.overrideWidth
                : displayInfo.physicalWidth);
        appendLine(builder, "effectiveHeight", displayInfo.overrideHeight > 0
                ? displayInfo.overrideHeight
                : displayInfo.physicalHeight);
        appendLine(builder, "physicalDensity", displayInfo.physicalDensity);
        appendLine(builder, "overrideDensity", displayInfo.overrideDensity);
        appendLine(builder, "effectiveDensity", displayInfo.overrideDensity > 0
                ? displayInfo.overrideDensity
                : displayInfo.physicalDensity);
        appendOptionalIntLine(builder, "brightness", displayInfo.brightness);
        appendOptionalIntLine(builder, "brightnessMode", displayInfo.brightnessMode);
        if (displayInfo.brightnessMode >= 0) {
            appendLine(builder, "autoBrightness", displayInfo.brightnessMode == 1 ? "true" : "false");
        }
        return RootCommandResult.success(builder.toString());
    }

    public static RootCommandResult setSize(int width, int height) {
        if (!isValidDisplaySize(width) || !isValidDisplaySize(height)) {
            return RootCommandResult.failure("display size is invalid");
        }

        return RootShellBridge.exec("wm size " + width + "x" + height, DEFAULT_TIMEOUT_MS);
    }

    public static RootCommandResult resetSize() {
        return RootShellBridge.exec("wm size reset", DEFAULT_TIMEOUT_MS);
    }

    public static RootCommandResult setDensity(int density) {
        if (density < MIN_DENSITY || density > MAX_DENSITY) {
            return RootCommandResult.failure("display density is invalid");
        }

        return RootShellBridge.exec("wm density " + density, DEFAULT_TIMEOUT_MS);
    }

    public static RootCommandResult resetDensity() {
        return RootShellBridge.exec("wm density reset", DEFAULT_TIMEOUT_MS);
    }

    public static RootCommandResult setBrightness(int brightness) {
        if (brightness < MIN_BRIGHTNESS || brightness > MAX_BRIGHTNESS) {
            return RootCommandResult.failure("brightness must be between 0 and 255");
        }

        return RootShellBridge.exec(
                "settings put system screen_brightness_mode 0"
                        + " && settings put system screen_brightness " + brightness,
                DEFAULT_TIMEOUT_MS
        );
    }

    public static RootCommandResult setAutoBrightness(boolean enabled) {
        return RootShellBridge.exec(
                "settings put system screen_brightness_mode " + (enabled ? 1 : 0),
                DEFAULT_TIMEOUT_MS
        );
    }

    private static DisplayInfo parseDisplayInfo(String text) {
        DisplayInfo info = new DisplayInfo();
        if (text == null || text.isEmpty()) {
            return info;
        }

        String[] lines = text.split("\\r?\\n");
        for (String line : lines) {
            String trimmed = line.trim();
            if (trimmed.startsWith("Physical size:")) {
                int[] size = parseSize(trimmed);
                info.physicalWidth = size[0];
                info.physicalHeight = size[1];
            } else if (trimmed.startsWith("Override size:")) {
                int[] size = parseSize(trimmed);
                info.overrideWidth = size[0];
                info.overrideHeight = size[1];
            } else if (trimmed.startsWith("Physical density:")) {
                info.physicalDensity = parseFirstInt(trimmed);
            } else if (trimmed.startsWith("Override density:")) {
                info.overrideDensity = parseFirstInt(trimmed);
            } else if (trimmed.startsWith("screen_brightness=")) {
                info.brightness = parseTailInt(trimmed, "screen_brightness=");
            } else if (trimmed.startsWith("screen_brightness_mode=")) {
                info.brightnessMode = parseTailInt(trimmed, "screen_brightness_mode=");
            }
        }
        return info;
    }

    private static int[] parseSize(String text) {
        Matcher matcher = SIZE_PATTERN.matcher(text);
        if (!matcher.find()) {
            return new int[]{0, 0};
        }
        return new int[]{
                parseInt(matcher.group(1), 0),
                parseInt(matcher.group(2), 0)
        };
    }

    private static int parseFirstInt(String text) {
        Matcher matcher = DENSITY_PATTERN.matcher(text);
        if (!matcher.find()) {
            return 0;
        }
        return parseInt(matcher.group(1), 0);
    }

    private static int parseTailInt(String text, String prefix) {
        String value = text.substring(prefix.length()).trim();
        if ("null".equals(value)) {
            return -1;
        }
        return parseInt(value, -1);
    }

    private static int parseInt(String value, int defaultValue) {
        try {
            return Integer.parseInt(value);
        } catch (NumberFormatException ignored) {
            return defaultValue;
        }
    }

    private static boolean isValidDisplaySize(int value) {
        return value >= MIN_DISPLAY_SIZE && value <= MAX_DISPLAY_SIZE;
    }

    private static void appendLine(StringBuilder builder, String key, int value) {
        if (value <= 0) {
            return;
        }

        builder.append(key)
                .append('=')
                .append(value)
                .append('\n');
    }

    /**
     * 亮度和亮度模式允许 0；用 -1 表示系统未返回或解析失败。
     */
    private static void appendOptionalIntLine(StringBuilder builder, String key, int value) {
        if (value < 0) {
            return;
        }

        builder.append(key)
                .append('=')
                .append(value)
                .append('\n');
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

    private static final class DisplayInfo {
        private int physicalWidth;
        private int physicalHeight;
        private int overrideWidth;
        private int overrideHeight;
        private int physicalDensity;
        private int overrideDensity;
        private int brightness = -1;
        private int brightnessMode = -1;
    }
}
