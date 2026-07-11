/**
 * 文件用途：实现稳定 C ABI 系统能力，供 Lua HostApi 和后续跨语言绑定复用。
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

// 当前截图点阵固定按 RGBA8888 保存，因此每个像素占 4 字节。
constexpr int kPixelBytes = 4;

// 默认 20ms 缓存窗口用于减少短时间内重复截图的开销。
constexpr int kDefaultCaptureCacheMs = 20;

// 对外暴露当前 native 能力边界，方便 IDE、插件或脚本运行时确认可用能力。
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
 * 把当前缓存帧写入 C ABI 输出参数。
 *
 * pixels 返回的是 gCapturePixels 的内部地址，调用方只读，不负责释放。
 */
void writeCaptureResultLocked(int* width, int* height, unsigned char** pixels) {
    *width = gCaptureWidth;
    *height = gCaptureHeight;
    *pixels = gCapturePixels.data();
}

/**
 * 记录最近一次错误并返回 false，便于校验函数直接 `return setErrorLocked(...)`。
 */
bool setErrorLocked(const std::string& error) {
    gLastError = error;
    return false;
}

/**
 * 校验 AndroidBridge 返回的截图结果能否写入 C ABI 缓存。
 *
 * 这里不做兜底路线，只判断当前结果是否满足 C ABI 的约定：宽高有效、点阵长度
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

/**
 * 返回引擎版本号。
 *
 * 返回值由 libengine.so 内部持有，调用方只读，不要释放。
 */
extern "C" const char* engine_version() {
    return EngineConfig::kEngineVersion;
}

/**
 * 返回当前 C ABI 能力描述 JSON。
 *
 * 这个接口用于外部调用方确认当前 so 支持哪些稳定能力，不参与脚本运行逻辑。
 */
extern "C" const char* engine_capabilities_json() {
    return kCapabilitiesJson;
}

/**
 * 获取屏幕截图。
 *
 * 成功时写出 width、height 和 pixels；pixels 指向内部缓存，格式固定为紧凑 RGBA。
 * 缓存可用时直接返回缓存，缓存不可用时调用 AndroidBridge 走 Root helper 截图。
 */
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

/**
 * 开启锁帧。
 *
 * 开启后 screen_capture 会一直复用当前缓存帧；如果当前还没有缓存帧，下一次
 * screen_capture 会先抓取一帧并把它锁住。
 */
extern "C" void screen_keep_capture() {
    std::lock_guard<std::mutex> lock(gCaptureMutex);
    gKeepCapture = true;
}

/**
 * 取消锁帧。
 *
 * 取消后 screen_capture 恢复按缓存时间判断是否重新截图。
 */
extern "C" void screen_release_capture() {
    std::lock_guard<std::mutex> lock(gCaptureMutex);
    gKeepCapture = false;
}

/**
 * 设置截图缓存时间，单位毫秒。
 *
 * 传入 0 表示每次调用都认为缓存立即过期；负数非法，会写入 screen_last_error。
 */
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

/**
 * 清空截图缓存并恢复默认缓存策略。
 *
 * 脚本开始和结束都会调用它，避免旧脚本留下的像素地址或锁帧状态影响新脚本。
 */
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

/**
 * 返回最近一次截图 C ABI 失败原因。
 *
 * 使用 thread_local 字符串承接全局错误文本，保证返回的 const char* 在本线程内
 * 下一次调用前保持有效。
 */
extern "C" const char* screen_last_error() {
    static thread_local std::string threadError;

    std::lock_guard<std::mutex> lock(gCaptureMutex);
    threadError = gLastError;
    return threadError.c_str();
}
