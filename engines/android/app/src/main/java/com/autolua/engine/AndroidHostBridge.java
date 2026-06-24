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

    public static RootCommandResult rootExec(String command, int timeoutMs) {
        return RootShellBridge.exec(command, timeoutMs);
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
