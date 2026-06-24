package com.autolua.engine;

/**
 * Java 平台能力桥。
 *
 * Native HostApi 需要调用 Android 系统能力时，统一通过这里进入 Java 层。
 * 这样 JNI 只依赖一个稳定类，不直接散落调用 Activity 或 Service。
 */
public final class AndroidHostBridge {
    private AndroidHostBridge() {
    }

    public static boolean isAccessibilityEnabled() {
        return AutomationAccessibilityService.isEnabled();
    }

    public static boolean isRootAvailable() {
        return RootShellBridge.isRootAvailable();
    }

    public static boolean touchTap(int x, int y) {
        if (RootShellBridge.tap(x, y)) {
            return true;
        }
        return AutomationAccessibilityService.tap(x, y);
    }

    public static boolean touchSwipe(int x1, int y1, int x2, int y2, int durationMs) {
        if (RootShellBridge.swipe(x1, y1, x2, y2, durationMs)) {
            return true;
        }
        return AutomationAccessibilityService.swipe(x1, y1, x2, y2, durationMs);
    }

    public static boolean keyBack() {
        if (RootShellBridge.keyBack()) {
            return true;
        }
        return AutomationAccessibilityService.back();
    }

    public static boolean keyHome() {
        if (RootShellBridge.keyHome()) {
            return true;
        }
        return AutomationAccessibilityService.home();
    }

    public static boolean hasScreenCapturePermission() {
        return RootShellBridge.isRootAvailable() || ScreenCaptureBridge.hasPermission();
    }

    public static ScreenCaptureResult captureScreen() {
        if (RootShellBridge.isRootAvailable()) {
            ScreenCaptureResult rootCapture = RootScreenCaptureBridge.captureFrame();
            if (rootCapture.success || !ScreenCaptureBridge.hasPermission()) {
                return rootCapture;
            }
        }
        return ScreenCaptureBridge.captureFrame();
    }
}
