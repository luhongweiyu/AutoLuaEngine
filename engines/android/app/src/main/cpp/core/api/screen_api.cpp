/**
 * 文件用途：实现屏幕截图核心 API，统一服务 Lua/JS/Go 绑定和 C ABI 门面。
 */
#include "screen_api.h"

#include <chrono>
#include <limits>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "../../platform/android_bridge.h"

namespace autolua::api {
namespace {

// 当前截图点阵固定按 RGBA8888 保存，因此每个像素占 4 字节。
constexpr int kPixelBytes = 4;

// 默认 20ms 缓存窗口用于减少短时间内重复截图的开销。
constexpr int kDefaultCaptureCacheMs = 20;

// 截图缓存的所有状态都由 gCaptureMutex 保护。
// pixels 指针会直接返回给调用方，所以 vector 只能在持锁状态下替换或清空。
std::mutex gCaptureMutex;
std::vector<unsigned char> gCapturePixels;
int gCaptureWidth = 0;
int gCaptureHeight = 0;
long long gCaptureStoredAtMs = 0;
int gCaptureCacheMs = kDefaultCaptureCacheMs;
bool gKeepCapture = false;
std::string gLastError;

/**
 * 返回单调递增时间，单位毫秒。
 *
 * 截图缓存只关心相对耗时，使用 steady_clock 可以避免系统时间被用户或系统校准后
 * 造成缓存立即过期或长时间不过期。
 */
long long steadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

/**
 * 判断当前是否已经有一帧可返回的截图缓存。
 *
 * 调用方必须已经持有 gCaptureMutex；函数名里的 Locked 用来提醒不要在未加锁
 * 的情况下读取宽高和 vector 状态。
 */
bool hasCachedFrameLocked() {
    return gCaptureWidth > 0 && gCaptureHeight > 0 && !gCapturePixels.empty();
}

/**
 * 判断当前缓存帧是否可以复用。
 *
 * 锁帧模式下只要存在缓存就一直复用；非锁帧模式下按 gCaptureCacheMs 判断是否
 * 仍在有效时间窗口内。
 */
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

/**
 * 把当前缓存帧写入输出参数。
 *
 * pixels 返回的是 gCapturePixels 的内部地址，调用方只读，不负责释放。
 */
void writeCaptureResultLocked(ScreenFrame* frame) {
    frame->width = gCaptureWidth;
    frame->height = gCaptureHeight;
    frame->pixels = gCapturePixels.data();
}

/**
 * 记录最近一次错误并返回 false，便于校验函数直接 `return setErrorLocked(...)`。
 */
bool setErrorLocked(const std::string& error) {
    gLastError = error;
    return false;
}

/**
 * 校验 AndroidBridge 返回的截图结果能否写入截图缓存。
 *
 * 这里不做兜底路线，只判断当前结果是否满足核心 API 约定：宽高有效、点阵长度
 * 至少为 width * height * 4。
 */
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

bool captureScreen(ScreenFrame* frame) {
    std::lock_guard<std::mutex> lock(gCaptureMutex);

    if (frame == nullptr) {
        setErrorLocked("screen capture output frame is null");
        return false;
    }

    if (isCacheUsableLocked()) {
        writeCaptureResultLocked(frame);
        return true;
    }

    ScreenCaptureResult result = AndroidBridge::captureRootScreen();
    if (!validateCaptureResultLocked(result)) {
        frame->width = 0;
        frame->height = 0;
        frame->pixels = nullptr;
        return false;
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

    writeCaptureResultLocked(frame);
    return true;
}

void keepScreenCapture() {
    std::lock_guard<std::mutex> lock(gCaptureMutex);
    gKeepCapture = true;
}

void releaseScreenCapture() {
    std::lock_guard<std::mutex> lock(gCaptureMutex);
    gKeepCapture = false;
}

bool setScreenCaptureCacheMs(int durationMs) {
    std::lock_guard<std::mutex> lock(gCaptureMutex);

    if (durationMs < 0) {
        setErrorLocked("capture cache ms must be greater than or equal to 0");
        return false;
    }

    gCaptureCacheMs = durationMs;
    gLastError.clear();
    return true;
}

void clearScreenCaptureCache() {
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

std::string screenLastError() {
    std::lock_guard<std::mutex> lock(gCaptureMutex);
    return gLastError;
}

} // namespace autolua::api
