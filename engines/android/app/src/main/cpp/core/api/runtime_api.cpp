/**
 * 文件用途：实现脚本运行时通用核心 API，例如日志输出和可中断 sleep。
 */
#include "runtime_api.h"

#include <algorithm>
#include <android/log.h>
#include <chrono>
#include <thread>

#include "../../runtime/common/log_buffer.h"

namespace autolua::api {
namespace {

constexpr const char* kLogTag = "AutoLuaEngine";

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

} // namespace autolua::api
