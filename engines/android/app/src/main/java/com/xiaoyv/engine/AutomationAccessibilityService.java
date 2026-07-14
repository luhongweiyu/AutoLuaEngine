/**
 * 文件用途：提供无障碍服务状态检测，并在非 Root 模式下接收全局音量键控制事件。
 */
package com.xiaoyv.engine;

import android.accessibilityservice.AccessibilityService;
import android.view.KeyEvent;
import android.view.accessibility.AccessibilityEvent;

/**
 * 自动化无障碍服务。
 *
 * App 设置页通过它展示无障碍服务状态。非 Root 模式下，它还负责接收全局音量键：
 * 音量加运行当前脚本，音量减停止脚本。Root 模式由常驻 RootDaemon 直接监听输入设备，
 * 此处只消费同一按键，避免系统同时修改媒体音量，不重复发送脚本控制命令。
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

    /**
     * 接收系统级硬件按键。
     *
     * 只处理音量加减，并且只在首次按下时发送一次命令；长按产生的重复 DOWN 事件不会
     * 反复运行或停止脚本。返回 true 会阻止受控按键继续修改系统媒体音量。
     */
    @Override
    protected boolean onKeyEvent(KeyEvent event) {
        int keyCode = event == null ? KeyEvent.KEYCODE_UNKNOWN : event.getKeyCode();
        boolean isVolumeKey = keyCode == KeyEvent.KEYCODE_VOLUME_UP
                || keyCode == KeyEvent.KEYCODE_VOLUME_DOWN;
        if (!isVolumeKey || !EngineSettings.isVolumeKeyControlEnabled(this)) {
            return super.onKeyEvent(event);
        }

        if (event.getAction() == KeyEvent.ACTION_DOWN
                && event.getRepeatCount() == 0
                && !EngineSettings.isRootModeEnabled(this)) {
            VolumeKeyController.handleKeyDown(this, keyCode);
        }
        return true;
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
