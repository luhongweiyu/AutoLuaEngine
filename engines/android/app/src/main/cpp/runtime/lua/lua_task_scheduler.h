/**
 * 文件用途：声明 Lua 多任务调度器，把真实 native 线程与共享 Lua VM 子状态配对管理。
 */
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

struct lua_State;
class LuaRuntime;

/**
 * LuaTaskScheduler 管理一个 LuaRuntime 内的主任务和全部子线程。
 *
 * 每个子任务都有自己的 std::thread 和 lua_newthread 子状态；所有子状态共享根 VM、
 * _G 和 registry。LuaVmGate 保证同一时刻只有一个任务操作 Lua VM，阻塞函数会在
 * 参数复制完成后临时释放 Gate，让其他任务继续执行。
 */
class LuaTaskScheduler {
public:
    static constexpr int kMaxChildThreads = 10;

    LuaTaskScheduler(lua_State* rootState, LuaRuntime* runtime);
    ~LuaTaskScheduler();

    LuaTaskScheduler(const LuaTaskScheduler&) = delete;
    LuaTaskScheduler& operator=(const LuaTaskScheduler&) = delete;

    /**
     * 执行根状态栈顶已经加载完成的初始化代码。
     *
     * 根状态只执行运行时引导，不执行用户主脚本，因此后续 Java 回调始终可以在空闲的
     * 根状态上安全调用 Lua 函数。
     */
    std::string runBootstrap();

    /**
     * 执行主脚本子状态栈顶的已加载函数，并在返回前停止、回收全部子线程。
     */
    std::string runMain(lua_State* mainState, int registryReference);

    /**
     * 从调用者 Lua 栈复制 callback 和可变参数，创建并启动一个 native 子线程。
     *
     * 成功返回大于 0 的任务 ID；失败返回 0，并把可显示错误写入 error。
     */
    long long startChild(
            lua_State* caller,
            int callbackIndex,
            int argumentCount,
            std::string* error
    );

    /**
     * 请求一个子线程停止并等待其退出。
     *
     * 等待期间会释放 VM Gate，避免被停止的子线程因拿不到 Gate 而无法执行停止 hook。
     */
    bool stopChildAndWait(long long taskId, std::string* error);

    /** 返回指定 Lua 子状态是否收到局部停止请求。 */
    bool isTaskStopRequested(lua_State* state) const;

    /** 标记全部子线程停止，但不等待；用于 App 全局停止时立即唤醒等待任务。 */
    void requestStopAll();

    /** 停止并 join 全部子线程，在 lua_close 前完成所有 native 线程回收。 */
    void stopAndJoinAll();

    /**
     * 阻塞函数在不再访问 Lua 栈后调用，临时释放当前线程持有的 VM Gate。
     * 返回 true 表示确实释放，调用方必须在返回 Lua 前调用 reacquireAfterBlocking。
     */
    bool releaseForBlocking();

    /** 重新取得由 releaseForBlocking 释放的 VM Gate。 */
    void reacquireAfterBlocking(bool released);

    /**
     * Java 回调线程进入共享 Lua VM。
     *
     * 返回令牌由 leaveFromCallback 原样接收；令牌为 0 表示当前线程本来已经持有 Gate。
     */
    int enterFromCallback();

    /** Java 回调执行结束后按令牌恢复 Gate 和线程局部上下文。 */
    void leaveFromCallback(int token);

    /** 当前线程是否正在持有本调度器的 VM Gate。 */
    bool isVmOwnedByCurrentThread() const;

    /**
     * 控制 hook 的公平调度点。
     *
     * 只有其他任务已经等待 Gate 时才让出；单线程运行时只读取一个原子计数。
     */
    void yieldIfTaskWaiting();

private:
    enum class TaskState {
        Created,
        Running,
        Finished,
        Failed,
        Stopped
    };

    struct LuaTask {
        long long id = 0;
        lua_State* state = nullptr;
        int registryReference = -2;
        int argumentCount = 0;
        bool mainTask = false;
        std::atomic_bool stopRequested{false};
        std::atomic_bool finished{false};
        std::atomic<TaskState> status{TaskState::Created};
        std::thread worker;
        std::mutex finishMutex;
        std::condition_variable finishCondition;
        std::mutex joinMutex;
    };

    /**
     * FIFO VM Gate。
     *
     * 使用票号保证释放 Gate 的睡眠任务重新竞争时排在已经等待的任务后面，避免同一个
     * 高频任务反复抢回 Gate 导致其他 Lua 线程饥饿。
     */
    class LuaVmGate {
    public:
        void lock();
        void unlock();
        bool hasWaiter() const;

    private:
        mutable std::mutex mutex_;
        std::condition_variable condition_;
        std::uint64_t nextTicket_ = 0;
        std::uint64_t servingTicket_ = 0;
        bool owned_ = false;
        std::atomic_int waiterCount_{0};
    };

    lua_State* rootState_;
    LuaRuntime* runtime_;
    LuaVmGate vmGate_;
    std::atomic_llong nextTaskId_{1};
    std::atomic_int activeChildCount_{0};
    mutable std::mutex tasksMutex_;
    std::unordered_map<long long, std::shared_ptr<LuaTask>> tasks_;

    std::shared_ptr<LuaTask> createTask(
            lua_State* state,
            int registryReference,
            int argumentCount,
            bool mainTask
    );
    std::string executeTask(const std::shared_ptr<LuaTask>& task);
    void runChildWorker(const std::shared_ptr<LuaTask>& task);
    void finishTask(const std::shared_ptr<LuaTask>& task, TaskState state);
    void joinTask(const std::shared_ptr<LuaTask>& task);
    void reapFinishedChildren();
    std::shared_ptr<LuaTask> findTask(long long taskId) const;
    static LuaTask* taskFromState(lua_State* state);
    static void bindTaskToState(lua_State* state, LuaTask* task);

    void enterVm(LuaTask* task);
    void leaveVm();
};
