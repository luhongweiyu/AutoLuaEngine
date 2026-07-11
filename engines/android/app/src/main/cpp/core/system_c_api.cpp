/**
 * 文件用途：实现稳定 C ABI 门面，把外部调用转发到 core/api 统一核心实现。
 */
#include "system_c_api.h"

#include <string>

#include "api/color_api.h"
#include "api/runtime_api.h"
#include "api/screen_api.h"
#include "../engine/engine_config.h"

namespace {

constexpr int kEngineAbiVersion = 4;

// 对外暴露当前 native 能力边界，方便 IDE、插件或脚本运行时确认可用能力。
constexpr const char* kCapabilitiesJson =
        "{"
        "\"abiVersion\":\"0.4\","
        "\"library\":\"libengine.so\","
        "\"core\":\"core/api + system_c_api\","
        "\"platform\":\"android\","
        "\"scriptBindings\":[\"lua\",\"js-reserved\",\"go-reserved\",\"plugin-reserved\"],"
        "\"pluginApi\":\"engine_get_api\","
        "\"runtimeApi\":[\"runtime_print\",\"runtime_log_print\",\"runtime_sleep\"],"
        "\"screenCapture\":[\"screen_capture\",\"screen_capture_cache\",\"screen_keep_capture\"],"
        "\"colorApi\":[\"color_find\"],"
        "\"imageFormat\":\"rgba8888\""
        "}";

thread_local std::string gRuntimeLastError;
thread_local std::string gScreenLastError;
thread_local std::string gColorLastError;

struct CInterruptContext {
    runtime_interrupt_callback callback = nullptr;
    void* userData = nullptr;
};

bool setRuntimeError(const std::string& error) {
    gRuntimeLastError = error;
    return false;
}

bool cInterruptAdapter(void* context) {
    auto* interrupt = static_cast<CInterruptContext*>(context);
    return interrupt != nullptr
            && interrupt->callback != nullptr
            && interrupt->callback(interrupt->userData) != 0;
}

const EngineApi kEngineApi = {
        kEngineAbiVersion,
        engine_version,
        engine_capabilities_json,
        runtime_print,
        runtime_log_print,
        runtime_sleep,
        runtime_sleep_interruptible,
        runtime_last_error,
        screen_capture,
        screen_keep_capture,
        screen_release_capture,
        screen_set_capture_cache_ms,
        screen_clear_capture_cache,
        screen_last_error,
        color_find,
        color_last_error
};

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
 * 返回外部插件 so 使用的引擎函数表。
 *
 * 这和旧项目 setLrApi 的思路一致，区别是这里由宿主主动导出完整函数表。
 */
extern "C" const EngineApi* engine_get_api() {
    return &kEngineApi;
}

/**
 * 输出普通脚本日志。
 *
 * Lua 的 print、后续 JS/Go 的同类输出都应该进入这个 C ABI，再由 runtime_api
 * 写入 Android logcat 和 native 日志缓冲。
 */
extern "C" int runtime_print(const char* text) {
    autolua::api::runtimePrint(text == nullptr ? "" : text);
    gRuntimeLastError.clear();
    return 1;
}

/**
 * 输出日志模块文本。
 *
 * 当前和 runtime_print 同级别输出，保留独立入口方便后续区分 print 与 log。
 */
extern "C" int runtime_log_print(const char* text) {
    autolua::api::runtimeLogPrint(text == nullptr ? "" : text);
    gRuntimeLastError.clear();
    return 1;
}

/**
 * 不带中断检查的睡眠。
 *
 * 适合没有脚本停止上下文的外部调用方；脚本运行时应优先使用
 * runtime_sleep_interruptible。
 */
extern "C" int runtime_sleep(int durationMs) {
    return runtime_sleep_interruptible(durationMs, nullptr, nullptr);
}

/**
 * 可中断睡眠。
 *
 * 语言绑定层传入 shouldInterrupt 回调，核心 runtime_api 只关心是否需要停止，
 * 不依赖具体脚本运行时类型。
 */
extern "C" int runtime_sleep_interruptible(
        int durationMs,
        runtime_interrupt_callback shouldInterrupt,
        void* userData
) {
    if (durationMs < 0) {
        setRuntimeError("sleep duration must be greater than or equal to 0");
        return 0;
    }

    CInterruptContext interrupt{shouldInterrupt, userData};
    bool ok = autolua::api::runtimeSleep(
            durationMs,
            shouldInterrupt == nullptr ? nullptr : cInterruptAdapter,
            shouldInterrupt == nullptr ? nullptr : &interrupt
    );

    if (!ok) {
        setRuntimeError("script stopped");
        return 0;
    }

    gRuntimeLastError.clear();
    return 1;
}

/**
 * 返回最近一次运行时 C ABI 失败原因。
 */
extern "C" const char* runtime_last_error() {
    return gRuntimeLastError.c_str();
}

/**
 * 获取屏幕截图。
 *
 * 成功时写出 width、height 和 pixels；pixels 指向内部缓存，格式固定为紧凑 RGBA。
 * 缓存、锁帧和 Root 截图分发都在 screen_api 中完成。
 */
extern "C" int screen_capture(int* width, int* height, unsigned char** pixels) {
    if (width == nullptr || height == nullptr || pixels == nullptr) {
        gScreenLastError = "screen capture output pointer is null";
        return 0;
    }

    autolua::api::ScreenFrame frame;
    if (!autolua::api::captureScreen(&frame)) {
        *width = 0;
        *height = 0;
        *pixels = nullptr;
        gScreenLastError = autolua::api::screenLastError();
        return 0;
    }

    *width = frame.width;
    *height = frame.height;
    *pixels = frame.pixels;
    gScreenLastError.clear();
    return 1;
}

/**
 * 开启锁帧。
 */
extern "C" void screen_keep_capture() {
    autolua::api::keepScreenCapture();
}

/**
 * 取消锁帧。
 */
extern "C" void screen_release_capture() {
    autolua::api::releaseScreenCapture();
}

/**
 * 设置截图缓存时间，单位毫秒。
 */
extern "C" int screen_set_capture_cache_ms(int durationMs) {
    if (!autolua::api::setScreenCaptureCacheMs(durationMs)) {
        gScreenLastError = autolua::api::screenLastError();
        return 0;
    }

    gScreenLastError.clear();
    return 1;
}

/**
 * 清空截图缓存并恢复默认缓存策略。
 */
extern "C" void screen_clear_capture_cache() {
    autolua::api::clearScreenCaptureCache();
    autolua::api::clearColorCache();
    gScreenLastError.clear();
}

/**
 * 返回最近一次截图 C ABI 失败原因。
 */
extern "C" const char* screen_last_error() {
    return gScreenLastError.c_str();
}

/**
 * 在当前屏幕截图缓存上执行多点找色。
 *
 * 找色核心内部会调用 screen_api，所以这里没有“是否截屏”参数；缓存策略统一由
 * screen_capture/keep/release/setCaptureCacheMs 控制。
 */
extern "C" int color_find(
        int x1,
        int y1,
        int x2,
        int y2,
        int dir,
        int sim,
        const char* colors,
        EnginePoint* point
) {
    if (point == nullptr) {
        gColorLastError = "color find output point is null";
        return 0;
    }

    autolua::api::ColorPoint colorPoint;
    bool found = autolua::api::findColorOnScreen(
            x1,
            y1,
            x2,
            y2,
            dir,
            sim,
            colors,
            &colorPoint
    );

    point->x = colorPoint.x;
    point->y = colorPoint.y;
    if (!found) {
        gColorLastError = autolua::api::colorLastError();
        return 0;
    }

    gColorLastError.clear();
    return 1;
}

/**
 * 返回最近一次找色 C ABI 失败原因。
 */
extern "C" const char* color_last_error() {
    return gColorLastError.c_str();
}
