/**
 * 文件用途：给 libengine.so 暴露 Android 平台状态、Root 初始化和 Root 截图入口。
 */
package com.xiaoyv.engine;

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
     * 锁定 小鱼精灵 输入法并保存原默认输入法。
     */
    public static boolean imeLock() {
        return EngineImeBridge.lock();
    }

    /**
     * 通过 小鱼精灵 输入法向当前焦点输入框提交完整 Unicode 文本。
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
     * 解码普通图片文件为紧凑 RGBA 点阵。
     *
     * 找图算法仍在 native core/api 中；Java 仅承担 Android 图片格式解码。
     */
    public static ImageDecodeResult decodeImageFile(String path) {
        return ImagePlatformBridge.decodeFile(path);
    }

    /**
     * 解码 native 提供的图片字节，用于 ALPKG 内未落地的资源图片。
     */
    public static ImageDecodeResult decodeImageBytes(ByteBuffer source, int size) {
        return ImagePlatformBridge.decodeBytes(source, size);
    }

    /**
     * 把 native RGBA 截图保存为普通图片文件。
     */
    public static boolean saveRgbaImage(ByteBuffer source, int width, int height, int size, String path) {
        return ImagePlatformBridge.saveRgba(source, width, height, size, path);
    }

    /**
     * 调用 RapidOCR ONNX 平台实现。
     *
     * 固定操作名和 JSON 参数都由 libengine.so/core/api 生成，Java 不向脚本开放任意
     * ONNX Runtime 反射入口。
     */
    public static String ocrCall(String operation, String argumentsJson) {
        return OcrPlatformBridge.call(operation, argumentsJson);
    }

    /**
     * 设备 API 的唯一 Java 平台入口。
     *
     * operation 由 libengine.so/core/api 固定生成，argumentsJson 由 native 完成结构化
     * 序列化；这里不向 Lua 暴露任意 Java 反射或 shell 调用能力。
     */
    public static String deviceCall(String operation, String argumentsJson) {
        return DevicePlatformBridge.call(appContext, operation, argumentsJson);
    }

    /**
     * 在 App 主进程创建脚本原生对话框悬浮层。
     *
     * 对话框直接由 WindowManager 覆盖在当前应用之上，不启动 Activity、不切换任务；
     * 框外触摸通过 FLAG_NOT_TOUCH_MODAL 继续交给下方应用。
     */
    public static boolean showScriptDialog(long sessionId, String specJson) {
        return ScriptDialogOverlayService.sendCommand(
                appContext,
                ScriptDialogOverlayService.ACTION_SHOW,
                sessionId,
                specJson
        );
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
     * 关闭一个脚本 UI 会话。网页 Activity 通过广播关闭，Dialog 和 HUD 由各自 Service 清理。
     */
    public static boolean closeScriptUi(long sessionId) {
        if (appContext == null || sessionId <= 0) {
            return false;
        }
        ScriptUiProtocol.sendClose(appContext, sessionId);
        ScriptDialogOverlayService.sendCommand(
                appContext,
                ScriptDialogOverlayService.ACTION_CLOSE,
                sessionId,
                "{}"
        );
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
        ScriptDialogOverlayService.sendCommand(
                appContext,
                ScriptDialogOverlayService.ACTION_CLOSE_ALL,
                0,
                "{}"
        );
        ScriptHudService.sendCommand(appContext, ScriptHudService.ACTION_CLOSE_ALL, 0, "{}");
    }
}
