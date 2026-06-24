#pragma once

#include <jni.h>
#include <string>
#include <vector>

struct ScreenCaptureResult {
    bool success = false;
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    int rowStride = 0;
    int pixelStride = 0;
    std::string format;
    std::string error;
};

/**
 * Android 平台能力桥。
 *
 * Native 层需要调用 Java/Kotlin 系统能力时统一从这里进入。
 * 当前用于无障碍状态检测和触控点击。
 */
class AndroidBridge {
public:
    static void init(JavaVM* javaVm);

    static bool isAccessibilityEnabled();
    static bool hasScreenCapturePermission();
    static ScreenCaptureResult captureScreen();
    static bool touchTap(int x, int y);
    static bool touchSwipe(int x1, int y1, int x2, int y2, int durationMs);
    static bool keyBack();
    static bool keyHome();

private:
};
