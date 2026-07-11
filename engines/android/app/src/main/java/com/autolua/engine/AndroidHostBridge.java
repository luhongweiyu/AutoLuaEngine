/**
 * 文件用途：给 libengine.so 暴露 Android 平台状态、Root 初始化和 Root 截图入口。
 */
package com.autolua.engine;

import android.content.Context;

import java.nio.ByteBuffer;

/**
 * Java 平台能力桥。
 *
 * JNI 层只依赖这个稳定类。当前阶段只保留引擎真正需要的入口：
 * 状态读取、Root 模式设置、Root helper 准备、截图和 Root 输入注入。
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

    /**
     * 返回引擎进程的 Application Context。
     *
     * Java 互操作层和兼容 LuaEngine.getContext() 需要返回真实 Android 对象；只暴露
     * Application Context，避免脚本长期持有 Activity 导致界面和资源泄漏。
     */
    public static Context applicationContext() {
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
        return captureRootScreen(null, 0);
    }

    /**
     * Root 截图入口。
     *
     * targetBuffer 来自 libengine.so 的截图缓存。容量足够时，Root helper 的像素流会
     * 直接读入该 native 缓冲，避免在 Java 引擎进程里再分配整帧 byte[]。
     */
    public static ScreenCaptureResult captureRootScreen(ByteBuffer targetBuffer, int targetCapacity) {
        return RootScreenCaptureBridge.captureFrame(targetBuffer, targetCapacity);
    }

    public static boolean touchDown(int id, int x, int y) {
        return RootHelperBridge.touchDown(id, x, y);
    }

    public static boolean touchMove(int id, int x, int y) {
        return RootHelperBridge.touchMove(id, x, y);
    }

    public static boolean touchUp(int id) {
        return RootHelperBridge.touchUp(id);
    }

    public static boolean keyDown(int keyCode) {
        return RootHelperBridge.keyDown(keyCode);
    }

    public static boolean keyUp(int keyCode) {
        return RootHelperBridge.keyUp(keyCode);
    }

    public static boolean keyPress(int keyCode) {
        return RootHelperBridge.keyPress(keyCode);
    }

    public static boolean inputText(String text) {
        return RootHelperBridge.inputText(text);
    }

    /**
     * 锁定 AutoLuaEngine 输入法并保存原默认输入法。
     */
    public static boolean imeLock() {
        return EngineImeBridge.lock();
    }

    /**
     * 通过 AutoLuaEngine 输入法向当前焦点输入框提交完整 Unicode 文本。
     */
    public static boolean imeSetText(String text) {
        return EngineImeBridge.setText(text);
    }

    /**
     * 恢复 lock 前保存的默认输入法。
     */
    public static boolean imeUnlock() {
        return EngineImeBridge.unlock();
    }
}
