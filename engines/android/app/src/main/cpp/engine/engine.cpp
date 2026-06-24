#include "engine.h"

#include "script_task.h"
#include "../runtime/lua_runtime.h"

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
          lastTaskId_(0),
          lastStatus_("idle") {
}

Engine::~Engine() = default;

void Engine::init() {
    initialized_ = true;
}

std::string Engine::runLuaText(const char* code) {
    if (!initialized_) {
        init();
    }

    stopRequested_.store(false);

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
    std::string result = runtime.runText(code, Engine::isStopRequested, this);
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
}

bool Engine::isStopRequested(void* context) {
    Engine* engine = static_cast<Engine*>(context);
    return engine != nullptr && engine->stopRequested_.load();
}
