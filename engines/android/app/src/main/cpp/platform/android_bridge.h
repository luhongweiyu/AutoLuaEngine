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
};
