/**
 * 文件用途：实现脚本运行时通用核心 API，例如日志输出和可中断 sleep。
 */
#include "runtime_api.h"

#include <algorithm>
#include <android/log.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#include "../../runtime/common/log_buffer.h"

namespace xiaoyv::api {
namespace {

constexpr const char* kLogTag = "小鱼精灵";
std::atomic_llong gScriptStartMs{0};
std::mutex gStopRequesterMutex;
RequestScriptStopCallback gStopRequester = nullptr;
void* gStopRequesterContext = nullptr;
std::mutex gScriptWorkPathMutex;
std::string gScriptWorkPath;

long long steadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

} // namespace

void runtimePrint(const std::string& message) {
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", message.c_str());
    appendLogEntry("info", message);
}

void runtimeLogPrint(const std::string& message) {
    runtimePrint(message);
}

bool runtimeSleep(long long durationMs, ShouldStopCallback shouldStop, void* stopContext) {
    if (durationMs < 0) {
        return false;
    }

    const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(durationMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (shouldStop != nullptr && shouldStop(stopContext)) {
            return false;
        }

        auto remaining = deadline - std::chrono::steady_clock::now();
        auto slice = std::min(
                std::chrono::duration_cast<std::chrono::milliseconds>(remaining),
                std::chrono::milliseconds(50)
        );
        if (slice.count() > 0) {
            std::this_thread::sleep_for(slice);
        }
    }

    return true;
}

void runtimeSetScriptStopRequester(RequestScriptStopCallback callback, void* context) {
    std::lock_guard<std::mutex> lock(gStopRequesterMutex);
    gStopRequester = callback;
    gStopRequesterContext = context;
}

bool runtimeRequestScriptStop() {
    RequestScriptStopCallback callback = nullptr;
    void* context = nullptr;
    {
        std::lock_guard<std::mutex> lock(gStopRequesterMutex);
        callback = gStopRequester;
        context = gStopRequesterContext;
    }
    return callback != nullptr && callback(context);
}

void runtimeMarkScriptStart() {
    gScriptStartMs.store(steadyNowMs());
}

void runtimeSetScriptWorkPath(const std::string& path) {
    std::lock_guard<std::mutex> lock(gScriptWorkPathMutex);
    gScriptWorkPath = path;
}

std::string runtimeScriptWorkPath() {
    std::lock_guard<std::mutex> lock(gScriptWorkPathMutex);
    return gScriptWorkPath;
}

long long runtimeSystemTimeMs() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

long long runtimeTickCountMs() {
    long long startMs = gScriptStartMs.load();
    if (startMs <= 0) {
        long long nowMs = steadyNowMs();
        long long expected = 0;
        gScriptStartMs.compare_exchange_strong(expected, nowMs);
        startMs = expected == 0 ? nowMs : expected;
    }
    return steadyNowMs() - startMs;
}

} // namespace xiaoyv::api
