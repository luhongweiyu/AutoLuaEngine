/**
 * 文件用途：实现引擎主对象，管理脚本任务、停止暂停状态和命令调用。
 */
#include "engine.h"

#include "script_task.h"
#include "../core/api/color_api.h"
#include "../core/api/image_api.h"
#include "../core/api/package_api.h"
#include "../core/api/runtime_api.h"
#include "../core/api/screen_api.h"
#include "../core/api/ui_api.h"
#include "../runtime/lua/lua_runtime.h"
#include "../runtime/lua/alpkg_package.h"

#include <sstream>

namespace {

// 这些文本会作为脚本运行结果返回给 App、IDE 和其他宿主；状态码仍由 statusJson 保持英文协议值。
constexpr const char* kAlreadyRunningMessage = "已有脚本正在运行";
constexpr const char* kLuaRunFailurePrefix = "Lua 执行失败：";
constexpr const char* kLuaLoadFailurePrefix = "Lua 加载失败：";

/**
 * 释放只属于一个脚本任务的 native 资源。
 *
 * 物理截图、图片屏幕、找色转置点阵和找图模板都不能跨脚本复用；统一在任务结束处释放，
 * 可以避免普通脚本或下一个 ALPKG 继承上一任务的点阵。UI 也在这里关闭，保证正常结束、
 * 外部停止、exitScript 和 Lua/native 异常都使用同一清理路径。
 */
void 清理脚本任务资源() {
    xiaoyv::api::closeAllUiSurfaces();
    xiaoyv::api::clearScreenCaptureCache();
    xiaoyv::api::清空找色缓存();
    xiaoyv::api::重置图片缓存();
}

/**
 * 让 getWorkPath 只在当前顶层脚本生命周期内可见。
 *
 * 脚本因 Lua 错误、native 异常或正常结束退出时都会自动清空路径，避免下一份脚本读到
 * 上一次任务的目录。
 */
class ScopedScriptWorkPath {
public:
    explicit ScopedScriptWorkPath(const std::string& path) {
        xiaoyv::api::runtimeSetScriptWorkPath(path);
    }

    ~ScopedScriptWorkPath() {
        xiaoyv::api::runtimeSetScriptWorkPath("");
    }
};

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
          activeLuaRuntime_(nullptr),
          lastTaskId_(0),
          lastStatus_("idle") {
}

Engine::~Engine() {
    xiaoyv::api::runtimeSetScriptStopRequester(nullptr, nullptr);
}

void Engine::init() {
    xiaoyv::api::runtimeSetScriptStopRequester(Engine::requestStopFromRuntime, this);
    initialized_ = true;
}

std::string Engine::runLuaText(const char* code, const std::string& workPath) {
    return runLuaInternal(nullptr, code, nullptr, workPath);
}

std::string Engine::runLuaPackage(
        const std::shared_ptr<AlpkgPackage>& package,
        const char* runtimeBootstrap,
        const std::string& workPath) {
    return runLuaInternal(package, nullptr, runtimeBootstrap, workPath);
}

std::string Engine::runLuaInternal(
        const std::shared_ptr<AlpkgPackage>& package,
        const char* code,
        const char* runtimeBootstrap,
        const std::string& workPath) {
    // 脚本任务必须在 native 层统一串行化。App、悬浮窗、HTTP 和后续插件都会
    // 进入同一个 libengine.so，如果只在某个 Java 入口加锁，其他入口仍会竞争
    // Lua VM 任务状态和停止/暂停控制位。
    std::unique_lock<std::mutex> runLock(runMutex_, std::try_to_lock);
    if (!runLock.owns_lock()) {
        return kAlreadyRunningMessage;
    }

    if (!initialized_) {
        init();
    }

    stopRequested_.store(false);
    pauseRequested_.store(false);
    controlCondition_.notify_all();
    xiaoyv::api::清空找色缓存();
    ScopedScriptWorkPath scriptWorkPath(workPath);

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

    // 将当前包绑定到脚本线程。Lua 绑定、未来 JS/Go 绑定和插件 C ABI 都只能读取
    // 当前任务的 resource 条目；普通脚本传入空包会主动清空上一个任务的上下文。
    xiaoyv::api::ScopedAlpkgPackageContext packageContext(package);
    LuaRuntime runtime;
    {
        std::lock_guard<std::mutex> lock(runtimeMutex_);
        activeLuaRuntime_ = &runtime;
    }
    std::string result;
    try {
        result = package == nullptr
                ? runtime.runText(code, Engine::shouldInterrupt, this)
                : runtime.runPackage(package, runtimeBootstrap, Engine::shouldInterrupt, this);
    } catch (...) {
        // activeLuaRuntime_ 只在 runtime 对象存活时有效。任何 native 异常向上返回前都要
        // 先解除注册，避免并发停止请求访问已经开始析构的运行时。
        {
            std::lock_guard<std::mutex> lock(runtimeMutex_);
            activeLuaRuntime_ = nullptr;
        }
        清理脚本任务资源();
        throw;
    }
    {
        std::lock_guard<std::mutex> lock(runtimeMutex_);
        activeLuaRuntime_ = nullptr;
    }
    清理脚本任务资源();
    pauseRequested_.store(false);
    controlCondition_.notify_all();

    // exitScript 和外部停止都会通过同一协作停止位终止 Lua。它们不是脚本错误，不能让
    // App 状态页显示“运行失败”；只有停止位未设置时的 Lua 加载/运行错误才标记失败。
    if (stopRequested_.load()) {
        task.markFinished("脚本已停止");
    } else if (result.rfind(kLuaRunFailurePrefix, 0) == 0
            || result.rfind(kLuaLoadFailurePrefix, 0) == 0) {
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
    std::string error = found ? lastError_ : "未找到脚本任务";

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

bool Engine::requestStop() {
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        if (!isActiveStatusLocked()) {
            return false;
        }

        // stopping 是终止中的明确状态。重复 stop 不重新修改控制位，调用方可以据此保持
        // UI 的“停止中”状态，而不是把没有运行中的任务误标成运行状态。
        lastStatus_ = "stopping";
        stopRequested_.store(true);
        pauseRequested_.store(false);
    }

    // 先设置全局停止位，再唤醒全部 Lua 子任务。每个任务会在 sleep/UI 等待回调或
    // 指令 hook 中看到同一停止状态并正常展开 Lua 栈。
    {
        std::lock_guard<std::mutex> lock(runtimeMutex_);
        if (activeLuaRuntime_ != nullptr) {
            activeLuaRuntime_->requestStopAllThreads();
        }
    }
    controlCondition_.notify_all();
    return true;
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

bool Engine::requestStopFromRuntime(void* context) {
    Engine* engine = static_cast<Engine*>(context);
    return engine != nullptr && engine->requestStop();
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
