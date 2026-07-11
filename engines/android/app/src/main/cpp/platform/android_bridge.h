/**
 * 文件用途：声明 libengine.so 访问 Android Java 层的最小平台桥。
 */
#pragma once

#include <jni.h>
#include <string>
#include <vector>

/**
 * 截图结果。
 *
 * 该结构只在 native 内部使用，C ABI 不直接暴露它。像素在 AndroidBridge 中会被
 * 整理成 width * height * 4 的紧凑 RGBA 缓冲，供 system_c_api 再返回给脚本层。
 */
struct ScreenCaptureResult {
    bool success = false;
    std::vector<unsigned char> pixels;
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
 * 这里是 libengine.so 到 Java 的唯一 JNI 边界。当前只保留引擎状态、Root 初始化
 * 和 Root 截图入口；其他系统能力未在此层暴露。
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

    static ScreenCaptureResult captureRootScreen();
};
