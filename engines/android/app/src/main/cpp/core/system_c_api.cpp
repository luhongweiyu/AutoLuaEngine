/**
 * 文件用途：实现给其他语言或 ffi 使用的 C ABI 系统 API 包装。
 */
#include "system_c_api.h"

#include <chrono>
#include <limits>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "../engine/engine_config.h"
#include "../platform/android_bridge.h"

namespace {

constexpr int kPixelBytes = 4;
constexpr int kDefaultCaptureCacheMs = 20;

constexpr const char* kCapabilitiesJson =
        "{"
        "\"abiVersion\":\"0.2\","
        "\"library\":\"libengine.so\","
        "\"core\":\"system_c_api\","
        "\"platform\":\"android\","
        "\"scriptBindings\":[\"lua\",\"js-reserved\",\"plugin-reserved\"],"
        "\"automationModes\":[\"root\"],"
        "\"rootCommandMode\":\"persistent-root-helper\","
        "\"screenCapture\":[\"root-capture-cache\"],"
        "\"imageFormat\":\"rgba8888\","
        "\"screenCaptureApi\":\"screen_capture(int* width,int* height,unsigned char** pixels)\""
        "}";

std::mutex gCaptureMutex;
std::vector<unsigned char> gCapturePixels;
int gCaptureWidth = 0;
int gCaptureHeight = 0;
long long gCaptureStoredAtMs = 0;
int gCaptureCacheMs = kDefaultCaptureCacheMs;
bool gKeepCapture = false;
std::string gLastError;

long long steadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

bool hasCachedFrameLocked() {
    return gCaptureWidth > 0 && gCaptureHeight > 0 && !gCapturePixels.empty();
}

bool isCacheUsableLocked() {
    if (!hasCachedFrameLocked()) {
        return false;
    }

    if (gKeepCapture) {
        return true;
    }

    long long ageMs = steadyNowMs() - gCaptureStoredAtMs;
    return ageMs >= 0 && ageMs <= gCaptureCacheMs;
}

void writeCaptureResultLocked(int* width, int* height, unsigned char** pixels) {
    *width = gCaptureWidth;
    *height = gCaptureHeight;
    *pixels = gCapturePixels.data();
}

bool setErrorLocked(const std::string& error) {
    gLastError = error;
    return false;
}

bool validateCaptureResultLocked(const ScreenCaptureResult& result) {
    if (!result.success) {
        return setErrorLocked(result.error.empty() ? "root capture failed" : result.error);
    }

    if (result.width <= 0 || result.height <= 0) {
        return setErrorLocked("root capture size is invalid");
    }

    long long expectedLength = static_cast<long long>(result.width)
            * static_cast<long long>(result.height)
            * static_cast<long long>(kPixelBytes);
    if (expectedLength <= 0 || expectedLength > std::numeric_limits<int>::max()) {
        return setErrorLocked("root capture pixel buffer is too large");
    }

    if (result.pixels.size() < static_cast<size_t>(expectedLength)) {
        return setErrorLocked("root capture pixel buffer is incomplete");
    }

    return true;
}

} // namespace

extern "C" const char* engine_version() {
    return EngineConfig::kEngineVersion;
}

extern "C" const char* engine_capabilities_json() {
    return kCapabilitiesJson;
}

extern "C" int screen_capture(int* width, int* height, unsigned char** pixels) {
    std::lock_guard<std::mutex> lock(gCaptureMutex);

    if (width == nullptr || height == nullptr || pixels == nullptr) {
        setErrorLocked("root capture output pointer is null");
        return 0;
    }

    if (isCacheUsableLocked()) {
        writeCaptureResultLocked(width, height, pixels);
        return 1;
    }

    ScreenCaptureResult result = AndroidBridge::captureRootScreen();
    if (!validateCaptureResultLocked(result)) {
        *width = 0;
        *height = 0;
        *pixels = nullptr;
        return 0;
    }

    size_t expectedLength = static_cast<size_t>(result.width)
            * static_cast<size_t>(result.height)
            * static_cast<size_t>(kPixelBytes);
    gCapturePixels = std::move(result.pixels);
    if (gCapturePixels.size() > expectedLength) {
        gCapturePixels.resize(expectedLength);
    }

    gCaptureWidth = result.width;
    gCaptureHeight = result.height;
    gCaptureStoredAtMs = steadyNowMs();
    gLastError.clear();

    writeCaptureResultLocked(width, height, pixels);
    return 1;
}

extern "C" void screen_keep_capture() {
    std::lock_guard<std::mutex> lock(gCaptureMutex);
    gKeepCapture = true;
}

extern "C" void screen_release_capture() {
    std::lock_guard<std::mutex> lock(gCaptureMutex);
    gKeepCapture = false;
}

extern "C" int screen_set_capture_cache_ms(int durationMs) {
    std::lock_guard<std::mutex> lock(gCaptureMutex);

    if (durationMs < 0) {
        setErrorLocked("capture cache ms must be greater than or equal to 0");
        return 0;
    }

    gCaptureCacheMs = durationMs;
    gLastError.clear();
    return 1;
}

extern "C" void screen_clear_capture_cache() {
    std::lock_guard<std::mutex> lock(gCaptureMutex);

    gCapturePixels.clear();
    gCapturePixels.shrink_to_fit();
    gCaptureWidth = 0;
    gCaptureHeight = 0;
    gCaptureStoredAtMs = 0;
    gCaptureCacheMs = kDefaultCaptureCacheMs;
    gKeepCapture = false;
    gLastError.clear();
}

extern "C" const char* screen_last_error() {
    static thread_local std::string threadError;

    std::lock_guard<std::mutex> lock(gCaptureMutex);
    threadError = gLastError;
    return threadError.c_str();
}
