package com.autolua.engine;

import android.content.Context;
import android.content.SharedPreferences;

/**
 * 引擎本地设置。
 *
 * 第一版先只保存 HTTP JSON-RPC 端口。监听地址仍固定为 127.0.0.1，
 * PC 端通过 adb forward 访问，避免调试服务意外暴露到局域网。
 * 悬浮窗位置和显示状态也放在这里，方便 MainActivity、悬浮窗服务共享同一份状态。
 */
public final class EngineSettings {
    public static final int DEFAULT_HTTP_PORT = 18380;

    private static final String PREFS_NAME = "engine_settings";
    private static final String KEY_HTTP_PORT = "http_port";
    private static final String KEY_FLOATING_BUBBLE_X = "floating_bubble_x";
    private static final String KEY_FLOATING_BUBBLE_Y = "floating_bubble_y";
    private static final String KEY_FLOATING_BUBBLE_HIDDEN = "floating_bubble_hidden";
    private static final String KEY_FLOATING_PANEL_EXPANDED = "floating_panel_expanded";
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

    /**
     * 读取悬浮按钮上次停靠的位置。
     *
     * 默认值由调用方传入，因为默认位置需要依赖当前屏幕尺寸和按钮尺寸。
     */
    public static int getFloatingBubbleX(Context context, int defaultValue) {
        return preferences(context).getInt(KEY_FLOATING_BUBBLE_X, defaultValue);
    }

    /**
     * 读取悬浮按钮上次停靠的纵向位置。
     */
    public static int getFloatingBubbleY(Context context, int defaultValue) {
        return preferences(context).getInt(KEY_FLOATING_BUBBLE_Y, defaultValue);
    }

    /**
     * 保存悬浮按钮最终位置。
     *
     * 只在松手吸边后保存，避免拖动过程中频繁写 SharedPreferences。
     */
    public static void setFloatingBubblePosition(Context context, int x, int y) {
        preferences(context)
                .edit()
                .putInt(KEY_FLOATING_BUBBLE_X, x)
                .putInt(KEY_FLOATING_BUBBLE_Y, y)
                .apply();
    }

    /**
     * 悬浮按钮是否被用户隐藏。
     *
     * 用户在面板里点击“隐藏”后会持久化为 true；从 App 再次点击“开启悬浮控制”会恢复。
     */
    public static boolean isFloatingBubbleHidden(Context context) {
        return preferences(context).getBoolean(KEY_FLOATING_BUBBLE_HIDDEN, false);
    }

    public static void setFloatingBubbleHidden(Context context, boolean hidden) {
        preferences(context)
                .edit()
                .putBoolean(KEY_FLOATING_BUBBLE_HIDDEN, hidden)
                .apply();
    }

    /**
     * 控制面板展开状态。
     *
     * 这个状态只用于服务被系统重建时尽量恢复用户刚才的浮窗形态。
     */
    public static boolean isFloatingPanelExpanded(Context context) {
        return preferences(context).getBoolean(KEY_FLOATING_PANEL_EXPANDED, false);
    }

    public static void setFloatingPanelExpanded(Context context, boolean expanded) {
        preferences(context)
                .edit()
                .putBoolean(KEY_FLOATING_PANEL_EXPANDED, expanded)
                .apply();
    }

    private static boolean isValidPort(int port) {
        return port >= MIN_PORT && port <= MAX_PORT;
    }

    private static SharedPreferences preferences(Context context) {
        return context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
    }
}
