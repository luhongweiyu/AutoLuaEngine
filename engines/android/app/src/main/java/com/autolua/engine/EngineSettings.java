package com.autolua.engine;

import android.content.Context;
import android.content.SharedPreferences;

/**
 * 引擎本地设置。
 *
 * 第一版先只保存 HTTP JSON-RPC 端口。监听地址仍固定为 127.0.0.1，
 * PC 端通过 adb forward 访问，避免调试服务意外暴露到局域网。
 */
public final class EngineSettings {
    public static final int DEFAULT_HTTP_PORT = 18380;

    private static final String PREFS_NAME = "engine_settings";
    private static final String KEY_HTTP_PORT = "http_port";
    private static final int MIN_PORT = 1024;
    private static final int MAX_PORT = 65535;

    private EngineSettings() {
    }

    public static int getHttpPort(Context context) {
        SharedPreferences preferences = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        int port = preferences.getInt(KEY_HTTP_PORT, DEFAULT_HTTP_PORT);
        return isValidPort(port) ? port : DEFAULT_HTTP_PORT;
    }

    public static void setHttpPort(Context context, int port) {
        if (!isValidPort(port)) {
            throw new IllegalArgumentException("http port must be between 1024 and 65535");
        }

        context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
                .edit()
                .putInt(KEY_HTTP_PORT, port)
                .apply();
    }

    private static boolean isValidPort(int port) {
        return port >= MIN_PORT && port <= MAX_PORT;
    }
}
