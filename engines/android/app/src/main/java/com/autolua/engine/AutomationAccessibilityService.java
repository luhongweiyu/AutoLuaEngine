/**
 * 文件用途：保留无障碍服务状态检测。
 */
package com.autolua.engine;

import android.accessibilityservice.AccessibilityService;
import android.view.accessibility.AccessibilityEvent;

/**
 * 自动化无障碍服务。
 *
 * 当前只用于 App 设置页展示无障碍服务是否已开启。
 * 服务必须由用户在系统设置中手动开启，App 不能静默开启无障碍权限。
 */
public final class AutomationAccessibilityService extends AccessibilityService {
    private static AutomationAccessibilityService instance;

    public static boolean isEnabled() {
        return instance != null;
    }

    @Override
    protected void onServiceConnected() {
        super.onServiceConnected();
        instance = this;
    }

    @Override
    public void onAccessibilityEvent(AccessibilityEvent event) {
        // 当前只保留服务状态，不处理无障碍事件。
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
