/**
 * 文件用途：给 libengine.so 暴露 Android 平台状态、Root 初始化和物理截图入口。
 */
package com.xiaoyv.engine;

import android.content.Context;
import android.content.ComponentName;
import android.net.Uri;
import android.os.Bundle;
import android.provider.Settings;

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
        Context application = context.getApplicationContext();
        appContext = application == null ? context : application;
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
        if (appContext == null) {
            return false;
        }
        try {
            if (Settings.Secure.getInt(
                    appContext.getContentResolver(),
                    Settings.Secure.ACCESSIBILITY_ENABLED,
                    0
            ) != 1) {
                return false;
            }
            String enabled = Settings.Secure.getString(
                    appContext.getContentResolver(),
                    Settings.Secure.ENABLED_ACCESSIBILITY_SERVICES
            );
            if (enabled == null || enabled.isEmpty()) {
                return false;
            }
            String expectedClass = AutomationAccessibilityService.class.getName();
            for (String item : enabled.split(":")) {
                ComponentName component = ComponentName.unflattenFromString(item);
                if (component != null
                        && appContext.getPackageName().equals(component.getPackageName())
                        && expectedClass.equals(component.getClassName())) {
                    return true;
                }
            }
        } catch (RuntimeException ignored) {
            // 系统设置不可读时按未启用返回，不改走其他探测路线。
        }
        return false;
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
        Bundle extras = new Bundle();
        extras.putBoolean(EngineWorkerBridgeProvider.EXTRA_ENABLED, enabled);
        return callControllerBoolean(EngineWorkerBridgeProvider.METHOD_SET_ROOT_MODE, extras);
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
        // 脚本 Worker 只能检查主进程已经准备好的 RootDaemon，不能在脚本路径重新执行 su。
        return RootDaemonClient.isReady(appContext);
    }

    public static boolean prepareRootHelper() {
        return RootHelperBridge.prepare();
    }

    /**
     * 当前 Worker 的物理截图入口。
     *
     * C ABI 的 engine_getScreenPixels 只走这里，不在失败时切换到其他截图路线。
     */
    public static ScreenCaptureResult captureScreen() {
        return captureScreen(null, 0, false);
    }

    /**
     * 当前 Worker 的物理截图入口。
     *
     * Root Worker 使用 SurfaceControl；App UID Worker 使用 MediaProjection/ImageReader。
     * 两条路线都优先直接写入 libengine.so 的固定 native 缓冲。
     */
    public static ScreenCaptureResult captureScreen(ByteBuffer targetBuffer, int targetCapacity) {
        return captureScreen(targetBuffer, targetCapacity, true);
    }

    public static ScreenCaptureResult captureScreen(
            ByteBuffer targetBuffer,
            int targetCapacity,
            boolean allowCachedNativeFrame
    ) {
        if (android.os.Process.myUid() == 0) {
            return RootScreenCaptureBridge.captureFrame(targetBuffer, targetCapacity);
        }
        MediaProjectionScreenCaptureBridge.initialize(appContext);
        return MediaProjectionScreenCaptureBridge.captureFrame(
                targetBuffer,
                targetCapacity,
                allowCachedNativeFrame
        );
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
    public static boolean saveRgbaImage(
            ByteBuffer source,
            int width,
            int height,
            int size,
            int left,
            int top,
            int right,
            int bottom,
            String path
    ) {
        return ImagePlatformBridge.saveRgba(
                source,
                width,
                height,
                size,
                left,
                top,
                right,
                bottom,
                path
        );
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
     * 调用可选 NCNN YOLO 平台实现。未导入 yolo/libxiaoyv_yolo.so 时返回可读的失败信封，
     * 不影响普通引擎启动或其他功能。
     */
    public static String yoloCall(String operation, String argumentsJson) {
        return YoloPlatformBridge.call(operation, argumentsJson);
    }

    /** 将 native 持有的紧凑 RGBA 直接缓冲交给可选 YOLO SO。 */
    public static String yoloDetectRgba(
            String operation,
            String argumentsJson,
            ByteBuffer pixels,
            int width,
            int height
    ) {
        return YoloPlatformBridge.detectRgba(operation, argumentsJson, pixels, width, height);
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

    static String readPasteboardFromController() {
        if (appContext == null) return "";
        try {
            Bundle result = ContentProviderBridge.call(
                    appContext,
                    controllerUri(),
                    EngineWorkerBridgeProvider.METHOD_READ_PASTEBOARD,
                    null,
                    null
            );
            return result == null ? "" : result.getString(EngineWorkerBridgeProvider.RESULT_TEXT, "");
        } catch (RuntimeException exception) {
            return "";
        }
    }

    static boolean writePasteboardThroughController(String text) {
        if (appContext == null) return false;
        Bundle extras = new Bundle();
        extras.putString(EngineWorkerBridgeProvider.EXTRA_TEXT, text == null ? "" : text);
        return callControllerBoolean(
                EngineWorkerBridgeProvider.METHOD_WRITE_PASTEBOARD,
                extras
        );
    }

    /**
     * 在 App 主进程创建脚本原生对话框悬浮层。
     *
     * 对话框直接由 WindowManager 覆盖在当前应用之上，不启动 Activity、不切换任务；
     * 框外触摸通过 FLAG_NOT_TOUCH_MODAL 继续交给下方应用。
     */
    public static boolean showScriptDialog(long sessionId, String specJson) {
        return callUiHost(EngineUiHost.DIALOG_SHOW, sessionId, specJson, false);
    }

    /**
     * 在 App 主进程创建脚本 HUD。
     */
    public static boolean showScriptHud(long sessionId, String specJson) {
        return callUiHost(EngineUiHost.HUD_SHOW, sessionId, specJson, false);
    }

    /**
     * 更新已有脚本 HUD。
     */
    public static boolean updateScriptHud(long sessionId, String specJson) {
        return callUiHost(EngineUiHost.HUD_UPDATE, sessionId, specJson, false);
    }

    /**
     * 在 App 主进程打开 HTML/WebView Activity。
     */
    public static boolean showScriptWeb(long sessionId, String specJson) {
        return callUiHost(EngineUiHost.WEB_SHOW, sessionId, specJson, false);
    }

    /**
     * 向指定 HTML 页面推送 JSON 消息。
     */
    public static boolean postScriptWebMessage(long sessionId, String messageJson) {
        return callUiHost(EngineUiHost.WEB_POST, sessionId, messageJson, false);
    }

    /**
     * 关闭一个脚本 UI 会话。网页 Activity 通过广播关闭，Dialog 和 HUD 由各自 Service 清理。
     */
    public static boolean closeScriptUi(long sessionId) {
        return callUiHost(EngineUiHost.UI_CLOSE, sessionId, "{}", false);
    }

    /** 返回设备是否声明 OpenGL ES 3，供 native imgui.isSupport() 快速检测。 */
    public static boolean isScriptImGuiSupported() {
        return ScriptImGuiService.isSupported(appContext);
    }

    /** 请求常驻 :engine 控制进程创建或替换 Dear ImGui 悬浮 Surface。 */
    public static boolean showScriptImGui(String configJson) {
        return callUiHost(EngineUiHost.IMGUI_SHOW, 0, configJson, false);
    }

    /** 更新独立 ImGui Surface 的位置和尺寸，不重建 EGLContext。 */
    public static boolean updateScriptImGui(String configJson) {
        return callUiHost(EngineUiHost.IMGUI_UPDATE, 0, configJson, false);
    }

    /** 关闭 ImGui Surface；服务不存在时该操作仍视为完成。 */
    public static boolean closeScriptImGui() {
        return callUiHost(EngineUiHost.IMGUI_CLOSE, 0, "{}", false);
    }

    /** 把 Dear ImGui 的 WantTextInput 状态转给当前输入代理。 */
    public static boolean setScriptImGuiKeyboardVisible(boolean visible) {
        return callUiHost(EngineUiHost.IMGUI_KEYBOARD, 0, "{}", visible);
    }

    /**
     * 强停引擎进程前关闭全部脚本界面，避免 UI 宿主留在屏幕上。
     */
    public static void closeAllScriptUi() {
        callUiHost(EngineUiHost.UI_CLOSE_ALL, 0, "{}", false);
    }

    private static boolean callUiHost(
            String action,
            long sessionId,
            String payload,
            boolean flag
    ) {
        if (appContext == null) return false;
        Bundle extras = new Bundle();
        extras.putString(EngineWorkerBridgeProvider.EXTRA_UI_ACTION, action);
        extras.putLong(EngineWorkerBridgeProvider.EXTRA_SESSION_ID, sessionId);
        extras.putString(
                EngineWorkerBridgeProvider.EXTRA_PAYLOAD,
                payload == null ? "{}" : payload
        );
        extras.putBoolean(EngineWorkerBridgeProvider.EXTRA_FLAG, flag);
        return callControllerBoolean(EngineWorkerBridgeProvider.METHOD_UI_HOST, extras);
    }

    private static boolean callControllerBoolean(String method, Bundle extras) {
        try {
            Bundle result = ContentProviderBridge.call(
                    appContext,
                    controllerUri(),
                    method,
                    null,
                    extras
            );
            return result != null
                    && result.getBoolean(EngineWorkerBridgeProvider.RESULT_ACCEPTED, false);
        } catch (RuntimeException exception) {
            return false;
        }
    }

    private static Uri controllerUri() {
        return Uri.parse("content://" + appContext.getPackageName() + ".engineworker");
    }
}
