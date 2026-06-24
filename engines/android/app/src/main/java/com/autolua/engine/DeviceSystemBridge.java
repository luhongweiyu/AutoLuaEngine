package com.autolua.engine;

import java.util.regex.Pattern;

/**
 * Android 系统设置和系统属性 root 桥。
 *
 * 这一层封装 `settings`、`getprop`、`setprop` 这类系统命令，作为后续显示、
 * 输入法、调试开关等 root 能力的底座。所有入参都先做白名单校验，再拼命令。
 */
public final class DeviceSystemBridge {
    private static final int DEFAULT_TIMEOUT_MS = 2500;
    private static final Pattern SETTINGS_NAMESPACE_PATTERN =
            Pattern.compile("system|secure|global");
    private static final Pattern SETTINGS_KEY_PATTERN =
            Pattern.compile("[A-Za-z0-9_.:-]+");
    private static final Pattern PROP_KEY_PATTERN =
            Pattern.compile("[A-Za-z0-9_.-]+");

    private DeviceSystemBridge() {
    }

    public static RootCommandResult settingsGet(String namespace, String key) {
        if (!isValidSettingsNamespace(namespace) || !isValidSettingsKey(key)) {
            return RootCommandResult.failure("settings namespace or key is invalid");
        }

        return RootShellBridge.exec(
                "settings get " + namespace + " " + key,
                DEFAULT_TIMEOUT_MS
        );
    }

    public static RootCommandResult settingsPut(String namespace, String key, String value) {
        if (!isValidSettingsNamespace(namespace) || !isValidSettingsKey(key) || value == null) {
            return RootCommandResult.failure("settings namespace, key or value is invalid");
        }

        return RootShellBridge.exec(
                "settings put " + namespace + " " + key + " " + RootShellBridge.shellQuote(value),
                DEFAULT_TIMEOUT_MS
        );
    }

    public static RootCommandResult settingsDelete(String namespace, String key) {
        if (!isValidSettingsNamespace(namespace) || !isValidSettingsKey(key)) {
            return RootCommandResult.failure("settings namespace or key is invalid");
        }

        return RootShellBridge.exec(
                "settings delete " + namespace + " " + key,
                DEFAULT_TIMEOUT_MS
        );
    }

    public static RootCommandResult propGet(String key) {
        if (!isValidPropKey(key)) {
            return RootCommandResult.failure("property key is invalid");
        }

        return RootShellBridge.exec(
                "getprop " + key,
                DEFAULT_TIMEOUT_MS
        );
    }

    public static RootCommandResult propSet(String key, String value) {
        if (!isValidPropKey(key) || value == null) {
            return RootCommandResult.failure("property key or value is invalid");
        }

        return RootShellBridge.exec(
                "setprop " + key + " " + RootShellBridge.shellQuote(value),
                DEFAULT_TIMEOUT_MS
        );
    }

    private static boolean isValidSettingsNamespace(String namespace) {
        return namespace != null && SETTINGS_NAMESPACE_PATTERN.matcher(namespace).matches();
    }

    private static boolean isValidSettingsKey(String key) {
        return key != null && SETTINGS_KEY_PATTERN.matcher(key).matches();
    }

    private static boolean isValidPropKey(String key) {
        return key != null && PROP_KEY_PATTERN.matcher(key).matches();
    }
}
