/**
 * 文件用途：给 libengine.so 暴露 Android 平台状态、Root 初始化和 Root 截图入口。
 */
package com.autolua.engine;

import android.content.Context;

/**
 * Java 平台能力桥。
 *
 * JNI 层只依赖这个稳定类。当前阶段只保留引擎真正需要的入口：
 * 状态读取、Root 模式设置、Root 运行层准备、Root helper 准备和截图。
 */
public final class AndroidHostBridge {
    private static Context appContext;

    private AndroidHostBridge() {
    }

    public static void init(Context context) {
        appContext = context.getApplicationContext();
    }

    static Context appContext() {
        return appContext;
    }

    public static boolean isAccessibilityEnabled() {
        return AutomationAccessibilityService.isEnabled();
    }

    public static int apiLevel() {
        return android.os.Build.VERSION.SDK_INT;
    }

    public static int httpPort() {
        return appContext == null
                ? EngineSettings.DEFAULT_HTTP_PORT
                : EngineSettings.getHttpPort(appContext);
    }

    public static String packageName() {
        return appContext == null ? "" : appContext.getPackageName();
    }

    public static boolean isRootModeEnabled() {
        return appContext == null || EngineSettings.isRootModeEnabled(appContext);
    }

    public static boolean setRootModeEnabled(boolean enabled) {
        if (appContext == null) {
            return false;
        }

        EngineSettings.setRootModeEnabled(appContext, enabled);
        return true;
    }

    public static boolean isRootAvailable() {
        return RootShellBridge.isRootAvailable();
    }

    public static RootStatus rootStatus() {
        return RootShellBridge.status();
    }

    public static boolean isRootRuntimeReady() {
        return RootShellBridge.isRootRuntimeReady();
    }

    public static boolean prepareRootRuntime() {
        return RootShellBridge.prepareRootRuntime().available;
    }

    public static boolean prepareRootHelper() {
        return RootHelperBridge.prepare();
    }

    /**
     * Root 截图入口。
     *
     * C ABI 的 screen_capture 只走这里，不在失败时切换到其他截图路线。
     */
    public static ScreenCaptureResult captureRootScreen() {
        return RootScreenCaptureBridge.captureFrame();
    }
}
