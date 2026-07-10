/**
 * 文件用途：实现引擎主对象，管理脚本任务、停止暂停状态和命令调用。
 */
#include "engine.h"

#include "script_task.h"
#include "../core/system_c_api.h"
#include "../runtime/lua/lua_runtime.h"

#include <sstream>

namespace {

std::string escapeJsonString(const std::string& text) {
    std::ostringstream output;

    for (char ch : text) {
        switch (ch) {
            case '\\':
                output << "\\\\";
                break;
            case '"':
                output << "\\\"";
                break;
            case '\n':
                output << "\\n";
                break;
            case '\r':
                output << "\\r";
                break;
            case '\t':
                output << "\\t";
                break;
            default:
                output << ch;
                break;
        }
    }

    return output.str();
}

} // namespace

Engine::Engine()
        : initialized_(false),
          nextTaskId_(1),
          stopRequested_(false),
          pauseRequested_(false),
          lastTaskId_(0),
          lastStatus_("idle") {
}

Engine::~Engine() = default;

void Engine::init() {
    initialized_ = true;
}

std::string Engine::runLuaText(const char* code) {
    // 脚本任务必须在 native 层统一串行化。App、悬浮窗、HTTP 和后续插件都会
    // 进入同一个 libengine.so，如果只在某个 Java 入口加锁，其他入口仍会竞争
    // Lua VM 任务状态和停止/暂停控制位。
    std::unique_lock<std::mutex> runLock(runMutex_, std::try_to_lock);
    if (!runLock.owns_lock()) {
        return "Engine is already running";
    }

    if (!initialized_) {
        init();
    }

    stopRequested_.store(false);
    pauseRequested_.store(false);
    controlCondition_.notify_all();
    screen_clear_capture_cache();

    int taskId;
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        taskId = nextTaskId_++;
        lastTaskId_ = taskId;
        lastStatus_ = "running";
        lastResult_.clear();
        lastError_.clear();
    }

    ScriptTask task(taskId);
    task.markRunning();

    LuaRuntime runtime;
    std::string result = runtime.runText(code, Engine::shouldInterrupt, this);
    screen_clear_capture_cache();
    pauseRequested_.store(false);
    controlCondition_.notify_all();

    if (result.rfind("Lua run failed:", 0) == 0 || result.rfind("Lua load failed:", 0) == 0) {
        task.markFailed(result);
    } else {
        task.markFinished(result);
    }

    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        lastTaskId_ = task.taskId();
        lastStatus_ = task.statusName();
        lastResult_ = task.result();
        lastError_ = task.error();
    }

    return task.summary();
}

std::string Engine::statusJson(int taskId) const {
    std::lock_guard<std::mutex> lock(taskMutex_);

    int resolvedTaskId = taskId == 0 ? lastTaskId_ : taskId;
    bool found = resolvedTaskId != 0 && resolvedTaskId == lastTaskId_;
    std::string status = found ? lastStatus_ : "unknown";
    std::string result = found ? lastResult_ : "";
    std::string error = found ? lastError_ : "task is not found";

    std::ostringstream output;
    output << "{";
    output << "\"taskId\":" << resolvedTaskId << ",";
    output << "\"status\":\"" << escapeJsonString(status) << "\",";
    output << "\"result\":\"" << escapeJsonString(result) << "\",";
    output << "\"error\":";
    if (error.empty()) {
        output << "null";
    } else {
        output << "\"" << escapeJsonString(error) << "\"";
    }
    output << "}";
    return output.str();
}

void Engine::requestStop() {
    stopRequested_.store(true);
    pauseRequested_.store(false);
    controlCondition_.notify_all();

    std::lock_guard<std::mutex> lock(taskMutex_);
    if (isActiveStatusLocked()) {
        lastStatus_ = "stopping";
    }
}

bool Engine::requestPause() {
    std::lock_guard<std::mutex> lock(taskMutex_);
    if (!isActiveStatusLocked()) {
        return false;
    }

    pauseRequested_.store(true);
    if (lastStatus_ == "running") {
        lastStatus_ = "pausing";
    }
    return true;
}

bool Engine::requestResume() {
    bool wasPaused = pauseRequested_.exchange(false);
    controlCondition_.notify_all();

    std::lock_guard<std::mutex> lock(taskMutex_);
    bool canResume = lastStatus_ == "pausing" || lastStatus_ == "paused";
    if (canResume) {
        lastStatus_ = "running";
    }
    return wasPaused || canResume;
}

bool Engine::shouldInterrupt(void* context) {
    Engine* engine = static_cast<Engine*>(context);
    return engine != nullptr && engine->waitIfPausedOrStopped();
}

bool Engine::waitIfPausedOrStopped() {
    if (stopRequested_.load()) {
        return true;
    }

    if (!pauseRequested_.load()) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        if (lastStatus_ == "running" || lastStatus_ == "pausing") {
            lastStatus_ = "paused";
        }
    }

    std::unique_lock<std::mutex> lock(controlMutex_);
    controlCondition_.wait(lock, [this]() {
        return !pauseRequested_.load() || stopRequested_.load();
    });

    if (stopRequested_.load()) {
        return true;
    }

    {
        std::lock_guard<std::mutex> taskLock(taskMutex_);
        if (lastStatus_ == "paused") {
            lastStatus_ = "running";
        }
    }
    return false;
}

bool Engine::isActiveStatusLocked() const {
    return lastStatus_ == "running"
            || lastStatus_ == "pausing"
            || lastStatus_ == "paused";
}
