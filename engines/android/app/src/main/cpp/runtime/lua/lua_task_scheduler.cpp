/**
 * 文件用途：实现 Lua native 子线程、共享 VM Gate、公平调度和线程生命周期回收。
 */
#include "lua_task_scheduler.h"

#include <android/log.h>
#include <vector>

#include "lua_runtime.h"
#include "../../core/api/package_api.h"
#include "../common/log_buffer.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

namespace {

constexpr const char* kLogTag = "AutoLuaEngine";

// 当前执行位置只属于 native 工作线程。阻塞期间保留 currentTask，停止回调仍能判断
// 应该终止哪个子任务；vmOwned 则明确区分当前是否可以访问 Lua VM。
thread_local LuaTaskScheduler* gCurrentScheduler = nullptr;
thread_local void* gCurrentTask = nullptr;
thread_local bool gVmOwned = false;

} // namespace

void LuaTaskScheduler::LuaVmGate::lock() {
    std::unique_lock<std::mutex> lock(mutex_);
    const std::uint64_t ticket = nextTicket_++;
    waiterCount_.fetch_add(1);
    condition_.wait(lock, [this, ticket]() {
        return !owned_ && ticket == servingTicket_;
    });
    waiterCount_.fetch_sub(1);
    owned_ = true;
}

void LuaTaskScheduler::LuaVmGate::unlock() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        owned_ = false;
        ++servingTicket_;
    }
    condition_.notify_all();
}

bool LuaTaskScheduler::LuaVmGate::hasWaiter() const {
    return waiterCount_.load() > 0;
}

LuaTaskScheduler::LuaTaskScheduler(lua_State* rootState, LuaRuntime* runtime)
        : rootState_(rootState), runtime_(runtime) {
    // lua_newthread 会复制根状态 extraspace 的初始内容。先明确写入空任务，避免子状态
    // 在绑定自己的 LuaTask 前继承未初始化指针。
    bindTaskToState(rootState_, nullptr);
}

LuaTaskScheduler::~LuaTaskScheduler() {
    stopAndJoinAll();
}

std::string LuaTaskScheduler::runBootstrap() {
    enterVm(nullptr);
    int callStatus = lua_pcall(rootState_, 0, 0, 0);
    std::string result;
    if (callStatus != LUA_OK) {
        const char* error = lua_tostring(rootState_, -1);
        result = error == nullptr ? "Lua 运行时引导执行失败" : error;
        lua_pop(rootState_, 1);
    }
    leaveVm();
    return result;
}

std::string LuaTaskScheduler::runMain(lua_State* mainState, int registryReference) {
    std::shared_ptr<LuaTask> task = createTask(mainState, registryReference, 0, true);
    std::string result = executeTask(task);

    // 主任务结束就代表整个脚本生命周期结束。先释放主任务持有的 Gate，再通知并等待
    // 子线程退出，否则子线程无法进入 hook 处理停止请求。
    stopAndJoinAll();
    return result;
}

long long LuaTaskScheduler::startChild(
        lua_State* caller,
        int callbackIndex,
        int argumentCount,
        std::string* error
) {
    if (caller == nullptr || runtime_ == nullptr) {
        if (error != nullptr) {
            *error = "Lua 多线程运行时不可用";
        }
        return 0;
    }
    if (!lua_isfunction(caller, callbackIndex)) {
        if (error != nullptr) {
            *error = "线程回调参数必须是 function";
        }
        return 0;
    }
    if (argumentCount < 0) {
        if (error != nullptr) {
            *error = "线程参数数量无效";
        }
        return 0;
    }

    // 已自然结束的 std::thread 必须及时 join；否则长时间脚本反复创建短任务时，系统
    // 线程句柄会一直积累到主脚本结束。Lua 线程对象只保存 ID，任务被回收后再次停止
    // 该对象会按“已经结束”处理。
    reapFinishedChildren();
    if (activeChildCount_.load() >= kMaxChildThreads) {
        if (error != nullptr) {
            *error = "同时运行的子线程不能超过 10 个";
        }
        return 0;
    }

    // lua_newthread 创建的子状态共享 _G 和 registry。registryReference 保证工作线程
    // 结束前子状态不会被 Lua GC 回收。
    lua_State* childState = lua_newthread(caller);
    if (childState == nullptr) {
        lua_pop(caller, 1);
        if (error != nullptr) {
            *error = "创建 Lua 子状态失败";
        }
        return 0;
    }
    int registryReference = luaL_ref(caller, LUA_REGISTRYINDEX);

    lua_pushvalue(caller, callbackIndex);
    for (int index = 0; index < argumentCount; ++index) {
        lua_pushvalue(caller, callbackIndex + 1 + index);
    }
    lua_xmove(caller, childState, argumentCount + 1);

    std::shared_ptr<LuaTask> task = createTask(
            childState,
            registryReference,
            argumentCount,
            false
    );
    activeChildCount_.fetch_add(1);
    try {
        task->worker = std::thread([this, task]() {
            runChildWorker(task);
        });
    } catch (...) {
        activeChildCount_.fetch_sub(1);
        luaL_unref(caller, LUA_REGISTRYINDEX, registryReference);
        {
            std::lock_guard<std::mutex> lock(tasksMutex_);
            tasks_.erase(task->id);
        }
        if (error != nullptr) {
            *error = "创建 native 子线程失败";
        }
        return 0;
    }

    if (error != nullptr) {
        error->clear();
    }
    return task->id;
}

bool LuaTaskScheduler::stopChildAndWait(long long taskId, std::string* error) {
    std::shared_ptr<LuaTask> task = findTask(taskId);
    if (task == nullptr || task->mainTask || task->finished.load()) {
        if (error != nullptr) {
            error->clear();
        }
        return true;
    }

    task->stopRequested.store(true);
    task->finishCondition.notify_all();

    if (gCurrentScheduler == this && gCurrentTask == task.get()) {
        if (error != nullptr) {
            *error = "当前线程已请求停止";
        }
        return false;
    }

    bool released = releaseForBlocking();
    {
        std::unique_lock<std::mutex> lock(task->finishMutex);
        task->finishCondition.wait(lock, [&task]() {
            return task->finished.load();
        });
    }
    joinTask(task);
    reacquireAfterBlocking(released);

    {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        tasks_.erase(taskId);
    }

    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool LuaTaskScheduler::isTaskStopRequested(lua_State* state) const {
    LuaTask* task = state == nullptr
            ? static_cast<LuaTask*>(gCurrentTask)
            : taskFromState(state);
    return task != nullptr && task->stopRequested.load();
}

void LuaTaskScheduler::requestStopAll() {
    std::vector<std::shared_ptr<LuaTask>> tasks;
    {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        tasks.reserve(tasks_.size());
        for (const auto& entry : tasks_) {
            if (!entry.second->mainTask && !entry.second->finished.load()) {
                tasks.push_back(entry.second);
            }
        }
    }

    for (const std::shared_ptr<LuaTask>& task : tasks) {
        task->stopRequested.store(true);
        task->finishCondition.notify_all();
    }
}

void LuaTaskScheduler::stopAndJoinAll() {
    requestStopAll();

    std::vector<std::shared_ptr<LuaTask>> tasks;
    {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        tasks.reserve(tasks_.size());
        for (const auto& entry : tasks_) {
            if (!entry.second->mainTask) {
                tasks.push_back(entry.second);
            }
        }
    }

    for (const std::shared_ptr<LuaTask>& task : tasks) {
        if (!task->finished.load()) {
            std::unique_lock<std::mutex> lock(task->finishMutex);
            task->finishCondition.wait(lock, [&task]() {
                return task->finished.load();
            });
        }
        joinTask(task);
    }
}

bool LuaTaskScheduler::releaseForBlocking() {
    if (gCurrentScheduler != this || !gVmOwned) {
        return false;
    }
    gVmOwned = false;
    vmGate_.unlock();
    return true;
}

void LuaTaskScheduler::reacquireAfterBlocking(bool released) {
    if (!released) {
        return;
    }
    vmGate_.lock();
    gVmOwned = true;
}

int LuaTaskScheduler::enterFromCallback() {
    if (gCurrentScheduler == this && gVmOwned) {
        return 0;
    }

    const bool attached = gCurrentScheduler == nullptr;
    vmGate_.lock();
    if (attached) {
        gCurrentScheduler = this;
        gCurrentTask = nullptr;
    }
    gVmOwned = true;
    return attached ? 2 : 1;
}

void LuaTaskScheduler::leaveFromCallback(int token) {
    if (token == 0) {
        return;
    }
    gVmOwned = false;
    vmGate_.unlock();
    if (token == 2) {
        gCurrentScheduler = nullptr;
        gCurrentTask = nullptr;
    }
}

bool LuaTaskScheduler::isVmOwnedByCurrentThread() const {
    return gCurrentScheduler == this && gVmOwned;
}

void LuaTaskScheduler::yieldIfTaskWaiting() {
    if (!vmGate_.hasWaiter() || gCurrentScheduler != this || !gVmOwned) {
        return;
    }

    bool released = releaseForBlocking();
    std::this_thread::yield();
    reacquireAfterBlocking(released);
}

std::shared_ptr<LuaTaskScheduler::LuaTask> LuaTaskScheduler::createTask(
        lua_State* state,
        int registryReference,
        int argumentCount,
        bool mainTask
) {
    std::shared_ptr<LuaTask> task = std::make_shared<LuaTask>();
    task->id = nextTaskId_.fetch_add(1);
    task->state = state;
    task->registryReference = registryReference;
    task->argumentCount = argumentCount;
    task->mainTask = mainTask;
    bindTaskToState(state, task.get());
    runtime_->configureTaskState(state);

    std::lock_guard<std::mutex> lock(tasksMutex_);
    tasks_[task->id] = task;
    return task;
}

std::string LuaTaskScheduler::executeTask(const std::shared_ptr<LuaTask>& task) {
    autolua::api::ScopedAlpkgPackageContext packageContext(runtime_->package());
    enterVm(task.get());
    task->status.store(TaskState::Running);

    int callStatus = lua_pcall(task->state, task->argumentCount, 0, 0);
    std::string error;
    TaskState finalState = TaskState::Finished;
    if (callStatus != LUA_OK) {
        const char* luaError = lua_tostring(task->state, -1);
        error = luaError == nullptr ? "Lua runtime error" : luaError;
        lua_pop(task->state, 1);
        finalState = task->stopRequested.load() || runtime_->shouldInterruptNow(task->state)
                ? TaskState::Stopped
                : TaskState::Failed;
    }

    // coroutine registry 引用必须在持有 Gate 时释放。释放后只保留任务状态和 native
    // thread 对象，Lua userdata 再次 stopThread 会安全地得到“已经结束”。
    if (task->registryReference != LUA_NOREF) {
        luaL_unref(rootState_, LUA_REGISTRYINDEX, task->registryReference);
        task->registryReference = LUA_NOREF;
    }
    task->state = nullptr;
    finishTask(task, finalState);
    leaveVm();

    if (finalState == TaskState::Failed) {
        return "Lua run failed: " + error;
    }
    if (finalState == TaskState::Stopped) {
        return "Lua run failed: script stopped";
    }
    return "Lua script OK";
}

void LuaTaskScheduler::runChildWorker(const std::shared_ptr<LuaTask>& task) {
    std::string result = executeTask(task);
    activeChildCount_.fetch_sub(1);

    if (task->status.load() == TaskState::Failed) {
        const std::string message = "Lua 子线程 "
                + std::to_string(task->id)
                + " 执行失败："
                + result;
        __android_log_print(
                ANDROID_LOG_ERROR,
                kLogTag,
                "%s",
                message.c_str()
        );
        appendLogEntry("error", message);
    }
}

void LuaTaskScheduler::finishTask(
        const std::shared_ptr<LuaTask>& task,
        TaskState state
) {
    {
        std::lock_guard<std::mutex> lock(task->finishMutex);
        task->status.store(state);
        task->finished.store(true);
    }
    task->finishCondition.notify_all();
}

void LuaTaskScheduler::joinTask(const std::shared_ptr<LuaTask>& task) {
    std::lock_guard<std::mutex> lock(task->joinMutex);
    if (task->worker.joinable() && task->worker.get_id() != std::this_thread::get_id()) {
        task->worker.join();
    }
}

void LuaTaskScheduler::reapFinishedChildren() {
    std::vector<std::shared_ptr<LuaTask>> finishedTasks;
    {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        for (const auto& entry : tasks_) {
            if (!entry.second->mainTask && entry.second->finished.load()) {
                finishedTasks.push_back(entry.second);
            }
        }
    }

    for (const std::shared_ptr<LuaTask>& task : finishedTasks) {
        joinTask(task);
    }

    if (!finishedTasks.empty()) {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        for (const std::shared_ptr<LuaTask>& task : finishedTasks) {
            tasks_.erase(task->id);
        }
    }
}

std::shared_ptr<LuaTaskScheduler::LuaTask> LuaTaskScheduler::findTask(long long taskId) const {
    std::lock_guard<std::mutex> lock(tasksMutex_);
    auto iterator = tasks_.find(taskId);
    return iterator == tasks_.end() ? nullptr : iterator->second;
}

LuaTaskScheduler::LuaTask* LuaTaskScheduler::taskFromState(lua_State* state) {
    if (state == nullptr) {
        return nullptr;
    }
    return *static_cast<LuaTask**>(lua_getextraspace(state));
}

void LuaTaskScheduler::bindTaskToState(lua_State* state, LuaTask* task) {
    *static_cast<LuaTask**>(lua_getextraspace(state)) = task;
}

void LuaTaskScheduler::enterVm(LuaTask* task) {
    vmGate_.lock();
    gCurrentScheduler = this;
    gCurrentTask = task;
    gVmOwned = true;
}

void LuaTaskScheduler::leaveVm() {
    gVmOwned = false;
    vmGate_.unlock();
    gCurrentTask = nullptr;
    gCurrentScheduler = nullptr;
}
