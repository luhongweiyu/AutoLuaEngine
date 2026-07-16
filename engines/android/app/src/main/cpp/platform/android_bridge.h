/**
 * 文件用途：声明 libengine.so 访问 Android Java 层的最小平台桥。
 */
#pragma once

#include <jni.h>
#include <cstddef>
#include <string>
#include <vector>

/**
 * 截图结果。
 *
 * 该结构只在 native 内部使用，C ABI 不直接暴露它。像素在 AndroidBridge 中会被
 * 整理成 width * height * 4 的紧凑 RGBA 缓冲，供 screen_api 缓存，再由 C ABI
 * 和语言绑定返回给调用方。
 */
struct ScreenCaptureResult {
    bool success = false;
    size_t pixelBytes = 0;
    int width = 0;
    int height = 0;
    int rowStride = 0;
    int pixelStride = 0;
    std::string source;
    long long captureDurationMs = 0;
    std::string error;
};

/**
 * Root 授权探测的一次尝试记录。
 */
struct RootProbeAttempt {
    std::string commandMode;
    std::string suPath;
    int exitCode = -1;
    std::string stdoutText;
    std::string stderrText;
    bool timedOut = false;
    std::string error;
};

/**
 * Root 运行层状态。
 */
struct RootStatusResult {
    bool available = false;
    std::string commandMode;
    std::string suPath;
    bool cached = false;
    long long cacheExpireAt = 0;
    std::string error;
    std::vector<RootProbeAttempt> attempts;
};

/**
 * Android 设备能力调用结果。
 *
 * Java 层统一返回 JSON 信封，core/api 在解析前先通过 invoked 区分 JNI 调用失败和
 * 设备能力本身失败。responseJson 只在 invoked 为 true 时有效。
 */
struct AndroidDeviceCallResult {
    bool invoked = false;
    std::string responseJson;
    std::string error;
};

/**
 * Android 图片解码结果。
 *
 * pixels 固定为紧凑 RGBA8888。图片文件由 Java Framework 解码，C++ 图像算法只接收
 * 统一点阵，不依赖 PNG/JPEG/WebP 等格式库。
 */
struct AndroidImageDecodeResult {
    bool success = false;
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels;
    long long sourceStamp = 0;
    std::string error;
};

/**
 * RapidOCR 平台调用结果。
 *
 * OCR 模型推理由 Android ONNX Runtime 完成；返回 JSON 信封后仍由 core/api 统一解析和
 * 对外暴露，避免 Lua/JS/Go 分别依赖 Java OCR 类型。
 */
struct AndroidOcrCallResult {
    bool invoked = false;
    std::string responseJson;
    std::string error;
};

/**
 * Android 平台能力桥。
 *
 * 这里是 libengine.so 到 Java 的唯一 JNI 边界。Java 只承接 Android 必须由
 * Framework 完成的桥接动作，脚本 API 的真实逻辑仍然放在 libengine.so/core/api。
 */
class AndroidBridge {
public:
    static void init(JavaVM* javaVm);

    static bool isAccessibilityEnabled();
    static int apiLevel();
    static int httpPort();
    static std::string packageName();

    static bool isRootModeEnabled();
    static bool setRootModeEnabled(bool enabled);
    static bool isRootAvailable();
    static bool isRootRuntimeReady();
    static bool prepareRootRuntime();
    static bool prepareRootHelper();
    static RootStatusResult rootStatus();

    static ScreenCaptureResult captureRootScreen(unsigned char** pixels, size_t* capacity);
    static AndroidImageDecodeResult decodeImageFile(const std::string& path);
    static AndroidImageDecodeResult decodeImageBytes(const unsigned char* data, size_t size);
    static bool saveRgbaImage(
            const unsigned char* pixels,
            int width,
            int height,
            size_t size,
            int left,
            int top,
            int right,
            int bottom,
            const std::string& path
    );
    static AndroidOcrCallResult callOcrApi(
            const std::string& operation,
            const std::string& argumentsJson
    );
    static bool touchDown(int id, int x, int y);
    static bool touchMove(int id, int x, int y);
    static bool touchUp(int id);
    static bool keyDown(int keyCode);
    static bool keyUp(int keyCode);
    static bool keyPress(int keyCode);
    static bool inputText(const std::string& text);
    static bool imeLock();
    static bool imeSetText(const std::string& text);
    static bool imeUnlock();

    /**
     * 调用 Android 平台设备能力。
     *
     * operation 和 argumentsJson 都由 core/api 生成，Java 侧只处理固定 operation，
     * 不允许 Lua/JS/Go 直接把 Java 方法名传入 JNI。
     */
    static AndroidDeviceCallResult callDeviceApi(
            const std::string& operation,
            const std::string& argumentsJson
    );

    /**
     * 在 App 主进程打开原生脚本弹窗、HUD 或 HTML 页面。
     *
     * libengine.so 只传递会话 ID 和 JSON 配置；Android 组件、窗口和 WebView 均由
     * Java 宿主处理，因此脚本引擎进程不会直接持有 Android View。
     */
    static bool showScriptDialog(long long sessionId, const std::string& specJson);
    static bool showScriptHud(long long sessionId, const std::string& specJson);
    static bool updateScriptHud(long long sessionId, const std::string& specJson);
    static bool showScriptWeb(long long sessionId, const std::string& specJson);
    static bool postScriptWebMessage(long long sessionId, const std::string& messageJson);
    static bool closeScriptUi(long long sessionId);
};
