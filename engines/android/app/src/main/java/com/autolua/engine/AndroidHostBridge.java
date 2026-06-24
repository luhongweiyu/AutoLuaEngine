package com.autolua.engine;

import android.content.Context;

/**
 * Java 平台能力桥。
 *
 * Native HostApi 需要调用 Android 系统能力时，统一通过这里进入 Java 层。
 * 这样 JNI 只依赖一个稳定类，不直接散落调用 Activity 或 Service。
 */
public final class AndroidHostBridge {
    private static Context appContext;

    private AndroidHostBridge() {
    }

    public static void init(Context context) {
        appContext = context.getApplicationContext();
    }

    public static boolean isAccessibilityEnabled() {
        return AutomationAccessibilityService.isEnabled();
    }

    public static boolean isRootAvailable() {
        return RootShellBridge.isRootAvailable();
    }

    public static boolean isRootModeEnabled() {
        return isRootModeEnabledInternal();
    }

    public static boolean setRootModeEnabled(boolean enabled) {
        if (appContext == null) {
            return false;
        }
        EngineSettings.setRootModeEnabled(appContext, enabled);
        return true;
    }

    public static RootCommandResult rootExec(String command, int timeoutMs) {
        return RootShellBridge.exec(command, timeoutMs);
    }

    public static RootCommandResult rootFileExists(String path) {
        return RootShellBridge.fileExists(path);
    }

    public static RootCommandResult rootFileReadText(String path, int timeoutMs) {
        return RootShellBridge.readTextFile(path, timeoutMs);
    }

    public static RootCommandResult rootFileWriteText(String path, String content, int timeoutMs) {
        return RootShellBridge.writeTextFile(path, content, timeoutMs);
    }

    public static RootCommandResult rootFileRemove(String path) {
        return RootShellBridge.removeFile(path);
    }

    public static boolean appIsInstalled(String packageName) {
        return appContext != null && AppControlBridge.isInstalled(appContext, packageName);
    }

    public static boolean appOpen(String packageName) {
        return appContext != null
                && AppControlBridge.open(appContext, packageName, isRootModeEnabledInternal());
    }

    public static boolean appStop(String packageName) {
        return AppControlBridge.stop(packageName);
    }

    public static boolean touchTap(int x, int y) {
        if (isRootModeEnabledInternal() && RootShellBridge.tap(x, y)) {
            return true;
        }
        return AutomationAccessibilityService.tap(x, y);
    }

    public static boolean touchSwipe(int x1, int y1, int x2, int y2, int durationMs) {
        if (isRootModeEnabledInternal() && RootShellBridge.swipe(x1, y1, x2, y2, durationMs)) {
            return true;
        }
        return AutomationAccessibilityService.swipe(x1, y1, x2, y2, durationMs);
    }

    public static boolean keyBack() {
        if (isRootModeEnabledInternal() && RootShellBridge.keyBack()) {
            return true;
        }
        return AutomationAccessibilityService.back();
    }

    public static boolean keyHome() {
        if (isRootModeEnabledInternal() && RootShellBridge.keyHome()) {
            return true;
        }
        return AutomationAccessibilityService.home();
    }

    /**
     * 执行通用 Android 按键码。
     *
     * 第一版优先走 root input keyevent；Back/Home 这两个系统键在 root 不可用时
     * 可以回退无障碍全局动作。其他 keyCode 暂不做模拟兜底，避免不同机型行为不一致。
     */
    public static boolean keyPress(int keyCode) {
        if (isRootModeEnabledInternal() && RootShellBridge.keyEvent(keyCode)) {
            return true;
        }
        if (keyCode == 4) {
            return AutomationAccessibilityService.back();
        }
        if (keyCode == 3) {
            return AutomationAccessibilityService.home();
        }
        return false;
    }

    /**
     * 向当前焦点输入框输入文本。
     *
     * 当前只启用 root 路线，因为无障碍文本注入需要焦点节点、输入控件类型等额外判断。
     * 中文、换行和复杂特殊字符后续通过输入法或剪贴板路线单独增强。
     */
    public static boolean inputText(String text) {
        return isRootModeEnabledInternal() && RootShellBridge.inputText(text);
    }

    public static boolean hasScreenCapturePermission() {
        return (isRootModeEnabledInternal() && RootShellBridge.isRootAvailable())
                || ScreenCaptureBridge.hasPermission();
    }

    public static ScreenCaptureResult captureScreen() {
        if (isRootModeEnabledInternal() && RootShellBridge.isRootAvailable()) {
            ScreenCaptureResult rootCapture = RootScreenCaptureBridge.captureFrame();
            if (rootCapture.success || !ScreenCaptureBridge.hasPermission()) {
                return rootCapture;
            }
        }
        return ScreenCaptureBridge.captureFrame();
    }

    private static boolean isRootModeEnabledInternal() {
        return appContext == null || EngineSettings.isRootModeEnabled(appContext);
    }
}
