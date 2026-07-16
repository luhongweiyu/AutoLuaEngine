/**
 * 文件用途：实现物理截图缓存和图片屏幕切换，统一服务 Lua/JS/Go 绑定与 C ABI 门面。
 */
#include "screen_api.h"

#include <chrono>
#include <cstdlib>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "../../platform/android_bridge.h"

namespace xiaoyv::api {
namespace {

// 当前截图点阵固定按 RGBA8888 保存，因此每个像素占 4 字节。
constexpr int kPixelBytes = 4;

// 默认 20ms 缓存窗口用于减少短时间内重复截图的开销。
constexpr int kDefaultCaptureCacheMs = 20;

// 截图缓存的所有状态都由 gCaptureMutex 保护。
// pixels 指针会直接返回给调用方，所以只能在持锁状态下替换、扩容或任务结束后释放。
std::mutex gCaptureMutex;
unsigned char* gCapturePixels = nullptr;
size_t gCaptureCapacity = 0;
size_t gCaptureSize = 0;
int gCaptureWidth = 0;
int gCaptureHeight = 0;
long long gCaptureStoredAtMs = 0;
int gCaptureCacheMs = kDefaultCaptureCacheMs;
bool gKeepCapture = false;
long long gCaptureFrameId = 0;
std::string gLastError;

// 图片屏幕与系统截图使用两块独立内存。活动期间只切换读取来源，不覆盖 gCapturePixels；
// shared_ptr 使并发中的旧 ScreenFrame 可以在还原或替换后继续安全读取。
std::shared_ptr<const std::vector<unsigned char>> gOverridePixels;
int gOverrideWidth = 0;
int gOverrideHeight = 0;

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
 * 的情况下读取宽高和裸指针状态。
 */
bool hasCachedFrameLocked() {
    return gCaptureWidth > 0 && gCaptureHeight > 0 && gCapturePixels != nullptr && gCaptureSize > 0;
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
 * 图片屏幕优先于系统截图。pixelOwner 在图片屏幕模式下持有点阵生命周期；系统截图模式下
 * pixels 返回 gCapturePixels，沿用现有只读、不释放的契约。
 */
void writeCaptureResultLocked(ScreenFrame* frame) {
    if (gOverridePixels != nullptr) {
        frame->width = gOverrideWidth;
        frame->height = gOverrideHeight;
        frame->pixels = const_cast<unsigned char*>(gOverridePixels->data());
        frame->frameId = gCaptureFrameId;
        frame->pixelOwner = gOverridePixels;
        return;
    }

    frame->width = gCaptureWidth;
    frame->height = gCaptureHeight;
    frame->pixels = gCapturePixels;
    frame->frameId = gCaptureFrameId;
    frame->pixelOwner.reset();
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
        return setErrorLocked(result.error.empty() ? "Root 截图失败" : result.error);
    }

    if (result.width <= 0 || result.height <= 0) {
        return setErrorLocked("Root 截图尺寸无效");
    }

    long long expectedLength = static_cast<long long>(result.width)
            * static_cast<long long>(result.height)
            * static_cast<long long>(kPixelBytes);
    if (expectedLength <= 0 || expectedLength > std::numeric_limits<int>::max()) {
        return setErrorLocked("Root 截图点阵缓冲过大");
    }

    if (result.pixelBytes < static_cast<size_t>(expectedLength)) {
        return setErrorLocked("Root 截图点阵缓冲不完整");
    }

    return true;
}

/**
 * 刷新物理屏幕缓冲。调用方必须持有 gCaptureMutex。
 *
 * 该函数从不读取或覆盖图片屏幕内存。
 */
bool refreshPhysicalScreenLocked() {
    ScreenCaptureResult result = AndroidBridge::captureRootScreen(&gCapturePixels, &gCaptureCapacity);
    if (!validateCaptureResultLocked(result)) {
        return false;
    }

    gCaptureSize = static_cast<size_t>(result.width)
            * static_cast<size_t>(result.height)
            * static_cast<size_t>(kPixelBytes);
    gCaptureWidth = result.width;
    gCaptureHeight = result.height;
    gCaptureStoredAtMs = steadyNowMs();
    ++gCaptureFrameId;
    gLastError.clear();
    return true;
}

} // namespace

bool captureScreen(ScreenFrame* frame) {
    std::lock_guard<std::mutex> lock(gCaptureMutex);

    if (frame == nullptr) {
        setErrorLocked("截图输出帧不能为空");
        return false;
    }

    // 图片屏幕是显式设置的固定帧，不参与 20ms 缓存判断，也不会触发系统截图。
    if (gOverridePixels != nullptr) {
        writeCaptureResultLocked(frame);
        return true;
    }

    if (isCacheUsableLocked()) {
        writeCaptureResultLocked(frame);
        return true;
    }

    if (!refreshPhysicalScreenLocked()) {
        frame->width = 0;
        frame->height = 0;
        frame->pixels = nullptr;
        frame->frameId = gCaptureFrameId;
        frame->pixelOwner.reset();
        return false;
    }

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
        setErrorLocked("截图缓存时间必须大于等于 0 毫秒");
        return false;
    }

    gCaptureCacheMs = durationMs;
    gLastError.clear();
    return true;
}

bool setScreenPixelOverride(
        std::vector<unsigned char>&& pixels,
        int width,
        int height,
        int screenWidth,
        int screenHeight
) {
    if (width <= 0 || height <= 0) {
        std::lock_guard<std::mutex> lock(gCaptureMutex);
        return setErrorLocked("图片屏幕尺寸必须大于 0");
    }

    long long expectedLength = static_cast<long long>(width)
            * static_cast<long long>(height)
            * static_cast<long long>(kPixelBytes);
    if (expectedLength <= 0
            || static_cast<unsigned long long>(expectedLength) != pixels.size()) {
        std::lock_guard<std::mutex> lock(gCaptureMutex);
        return setErrorLocked("图片屏幕点阵长度与宽高不一致");
    }

    // 在加锁前完成共享对象分配；锁内只切换活动引用，避免图片内存分配阻塞高频读帧。
    if (screenWidth <= 0 || screenHeight <= 0) {
        std::lock_guard<std::mutex> lock(gCaptureMutex);
        return setErrorLocked("物理屏幕尺寸无效");
    }
    if (width > screenWidth || height > screenHeight) {
        std::lock_guard<std::mutex> lock(gCaptureMutex);
        return setErrorLocked(
                "图片尺寸超过屏幕范围：图片 "
                + std::to_string(width) + "x" + std::to_string(height)
                + "，屏幕 " + std::to_string(screenWidth) + "x" + std::to_string(screenHeight)
        );
    }

    auto newPixels = std::make_shared<const std::vector<unsigned char>>(std::move(pixels));
    std::lock_guard<std::mutex> lock(gCaptureMutex);
    gOverridePixels = std::move(newPixels);
    gOverrideWidth = width;
    gOverrideHeight = height;
    ++gCaptureFrameId;
    gLastError.clear();
    return true;
}

void restoreScreenPixelOverride() {
    std::lock_guard<std::mutex> lock(gCaptureMutex);
    if (gOverridePixels != nullptr) {
        gOverridePixels.reset();
        gOverrideWidth = 0;
        gOverrideHeight = 0;
        ++gCaptureFrameId;
    }
    gLastError.clear();
}

void clearScreenCaptureCache() {
    std::lock_guard<std::mutex> lock(gCaptureMutex);

    gOverridePixels.reset();
    gOverrideWidth = 0;
    gOverrideHeight = 0;
    std::free(gCapturePixels);
    gCapturePixels = nullptr;
    gCaptureCapacity = 0;
    gCaptureSize = 0;
    gCaptureWidth = 0;
    gCaptureHeight = 0;
    gCaptureStoredAtMs = 0;
    gCaptureCacheMs = kDefaultCaptureCacheMs;
    gKeepCapture = false;
    ++gCaptureFrameId;
    gLastError.clear();
}

std::string screenLastError() {
    std::lock_guard<std::mutex> lock(gCaptureMutex);
    return gLastError;
}

long long screenCaptureFrameId() {
    std::lock_guard<std::mutex> lock(gCaptureMutex);
    return gCaptureFrameId;
}

} // namespace xiaoyv::api
