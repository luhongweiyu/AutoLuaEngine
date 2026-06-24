package com.autolua.engine;

import android.accessibilityservice.AccessibilityService;
import android.accessibilityservice.GestureDescription;
import android.graphics.Path;
import android.os.Build;
import android.view.accessibility.AccessibilityEvent;

/**
 * 自动化无障碍服务。
 *
 * 第一版实现点击、滑动和基础全局按键，为 Lua 的 touch/key 模块提供 Android 平台能力。
 * 服务必须由用户在系统设置中手动开启，App 不能静默开启无障碍权限。
 */
public final class AutomationAccessibilityService extends AccessibilityService {
    private static AutomationAccessibilityService instance;

    public static boolean isEnabled() {
        return instance != null;
    }

    public static boolean tap(int x, int y) {
        AutomationAccessibilityService service = instance;
        if (service == null || Build.VERSION.SDK_INT < Build.VERSION_CODES.N) {
            return false;
        }

        Path path = new Path();
        path.moveTo(x, y);

        GestureDescription.StrokeDescription stroke =
                new GestureDescription.StrokeDescription(path, 0, 50);
        GestureDescription gesture = new GestureDescription.Builder()
                .addStroke(stroke)
                .build();

        return service.dispatchGesture(gesture, null, null);
    }

    public static boolean swipe(int x1, int y1, int x2, int y2, int durationMs) {
        AutomationAccessibilityService service = instance;
        if (service == null || Build.VERSION.SDK_INT < Build.VERSION_CODES.N) {
            return false;
        }

        Path path = new Path();
        path.moveTo(x1, y1);
        path.lineTo(x2, y2);

        GestureDescription.StrokeDescription stroke =
                new GestureDescription.StrokeDescription(path, 0, Math.max(durationMs, 1));
        GestureDescription gesture = new GestureDescription.Builder()
                .addStroke(stroke)
                .build();

        return service.dispatchGesture(gesture, null, null);
    }

    public static boolean back() {
        AutomationAccessibilityService service = instance;
        if (service == null) {
            return false;
        }

        // Android 无障碍全局动作由系统执行，比模拟坐标点击导航栏更稳定。
        return service.performGlobalAction(GLOBAL_ACTION_BACK);
    }

    public static boolean home() {
        AutomationAccessibilityService service = instance;
        if (service == null) {
            return false;
        }

        return service.performGlobalAction(GLOBAL_ACTION_HOME);
    }

    @Override
    protected void onServiceConnected() {
        super.onServiceConnected();
        instance = this;
    }

    @Override
    public void onAccessibilityEvent(AccessibilityEvent event) {
        // 第一版不处理事件，只使用 dispatchGesture 发送手势。
    }

    @Override
    public void onInterrupt() {
        // 无障碍服务被系统中断时无需额外处理。
    }

    @Override
    public boolean onUnbind(android.content.Intent intent) {
        instance = null;
        return super.onUnbind(intent);
    }
}
