/**
 * 文件用途：给 libengine.so 暴露 Android 平台状态、Root 初始化和 Root 截图入口。
 */
package com.autolua.engine;

import android.content.Context;
import android.content.Intent;

import java.nio.ByteBuffer;

/**
 * Java 平台能力桥。
 *
 * JNI 层只依赖这个稳定类。当前阶段只保留引擎真正需要的入口：
 * 状态读取、Root 模式设置、RootDaemon 连接、截图、Root 输入注入和脚本 UI 分发。
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
        RootDaemonService.setRootModeEnabled(appContext, enabled);
        return true;
    }

    public static boolean isRootAvailable() {
        return RootDaemonClient.status(appContext).available;
    }

    public static RootStatus rootStatus() {
        return RootDaemonClient.status(appContext);
    }

    public static boolean isRootRuntimeReady() {
        return RootDaemonClient.isReady(appContext);
    }

    public static boolean prepareRootRuntime() {
        // :engine 只能检查主进程已经准备好的 RootDaemon，不能在脚本路径重新执行 su。
        return RootDaemonClient.isReady(appContext);
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

    /**
     * 在 App 主进程打开原生弹窗 Activity。
     */
    public static boolean showScriptDialog(long sessionId, String specJson) {
        if (appContext == null || sessionId <= 0) {
            return false;
        }
        Intent intent = new Intent(appContext, ScriptDialogActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        intent.putExtra(ScriptUiProtocol.EXTRA_SESSION_ID, sessionId);
        intent.putExtra(ScriptUiProtocol.EXTRA_SPEC_JSON, specJson == null ? "{}" : specJson);
        try {
            appContext.startActivity(intent);
            return true;
        } catch (RuntimeException exception) {
            return false;
        }
    }

    /**
     * 在 App 主进程创建脚本 HUD。
     */
    public static boolean showScriptHud(long sessionId, String specJson) {
        return ScriptHudService.sendCommand(appContext, ScriptHudService.ACTION_SHOW, sessionId, specJson);
    }

    /**
     * 更新已有脚本 HUD。
     */
    public static boolean updateScriptHud(long sessionId, String specJson) {
        return ScriptHudService.sendCommand(appContext, ScriptHudService.ACTION_UPDATE, sessionId, specJson);
    }

    /**
     * 在 App 主进程打开 HTML/WebView Activity。
     */
    public static boolean showScriptWeb(long sessionId, String specJson) {
        if (appContext == null || sessionId <= 0) {
            return false;
        }
        Intent intent = new Intent(appContext, ScriptWebActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        intent.putExtra(ScriptUiProtocol.EXTRA_SESSION_ID, sessionId);
        intent.putExtra(ScriptUiProtocol.EXTRA_SPEC_JSON, specJson == null ? "{}" : specJson);
        try {
            appContext.startActivity(intent);
            return true;
        } catch (RuntimeException exception) {
            return false;
        }
    }

    /**
     * 向指定 HTML 页面推送 JSON 消息。
     */
    public static boolean postScriptWebMessage(long sessionId, String messageJson) {
        if (appContext == null || sessionId <= 0) {
            return false;
        }
        ScriptUiProtocol.sendWebMessage(appContext, sessionId, messageJson);
        return true;
    }

    /**
     * 关闭一个脚本 UI 会话。Activity 通过广播关闭，HUD 通过主进程 Service 命令关闭。
     */
    public static boolean closeScriptUi(long sessionId) {
        if (appContext == null || sessionId <= 0) {
            return false;
        }
        ScriptUiProtocol.sendClose(appContext, sessionId);
        ScriptHudService.sendCommand(appContext, ScriptHudService.ACTION_CLOSE, sessionId, "{}");
        return true;
    }

    /**
     * 强停引擎进程前关闭全部脚本界面，避免 UI 宿主留在屏幕上。
     */
    public static void closeAllScriptUi() {
        if (appContext == null) {
            return;
        }
        ScriptUiProtocol.sendCloseAll(appContext);
        ScriptHudService.sendCommand(appContext, ScriptHudService.ACTION_CLOSE_ALL, 0, "{}");
    }
}
