/**
 * 文件用途：声明 LuaRuntime，封装单个 Lua 虚拟机和脚本执行入口。
 */
#pragma once

#include <memory>
#include <string>

class AlpkgPackage;
class LuaTaskScheduler;

/**
 * LuaRuntime 负责管理一个共享 Lua 虚拟机和 LuaTaskScheduler。
 *
 * 根 lua_State 保存共享 _G、registry 和 Java 回调上下文；主脚本与子线程均运行在
 * lua_newthread 创建的子状态中。子状态由真实 native 线程驱动，并通过 VM Gate 串行
 * 访问同一个 Lua VM。
 */
class LuaRuntime {
public:
    LuaRuntime();
    ~LuaRuntime();

    LuaRuntime(const LuaRuntime&) = delete;
    LuaRuntime& operator=(const LuaRuntime&) = delete;

    /**
     * 执行一段 Lua 代码。
     *
     * @param code Lua 源码文本。
     * @param shouldInterrupt 返回 true 时中断脚本执行；内部也可以等待暂停恢复。
     * @return 执行成功返回结果摘要，失败返回错误信息。
     */
    std::string runText(const char* code, bool (*shouldInterrupt)(void*), void* controlContext);

    /**
     * 运行 ALPKG 包的入口字节码。
     *
     * 包内 Lua 文件按需认证解密后由 lua_load 读取，require/dofile/loadfile 也会走同一份
     * 包索引，不会把源码或字节码解压到 Android 共享存储。
     */
    std::string runPackage(
            const std::shared_ptr<AlpkgPackage>& package,
            const char* runtimeBootstrap,
            bool (*shouldInterrupt)(void*),
            void* controlContext
    );

    /**
     * 查询当前脚本是否应该中断。
     *
     * HostApi 的阻塞型函数（例如 sleep）会主动调用它，避免只有 VM hook
     * 执行到下一批 Lua 指令时才响应停止。
     */
    bool shouldInterruptNow(struct lua_State* state = nullptr) const;

    /** 从共享 registry 取得 state 所属 LuaRuntime。 */
    static LuaRuntime* fromState(struct lua_State* state);

    /** 为主任务或子任务状态安装停止检查和公平调度 hook。 */
    void configureTaskState(struct lua_State* state);

    /** 创建一个共享 _G 的真实 native 子线程。 */
    long long startChildThread(
            struct lua_State* caller,
            int callbackIndex,
            int argumentCount,
            std::string* error
    );

    /** 启动不占用用户线程名额的引擎内部 Lua 子任务，例如 ImGui 事件泵。 */
    long long startInternalChildThread(
            struct lua_State* caller,
            int callbackIndex,
            int argumentCount,
            std::string* error
    );

    /** 请求子线程停止并等待其退出。 */
    bool stopChildThread(long long taskId, std::string* error);

    /** App 全局停止时通知全部 Lua 子线程退出等待。 */
    void requestStopAllThreads();

    /** 阻塞 API 在不访问 Lua 栈期间临时释放 VM Gate。 */
    bool releaseVmForBlocking();

    /** 阻塞 API 返回 Lua 前重新取得 VM Gate。 */
    void reacquireVmAfterBlocking(bool released);

    /** Java 回调进入共享 VM，返回值必须传给 leaveVmFromCallback。 */
    int enterVmFromCallback();

    /** Java 回调离开共享 VM。 */
    void leaveVmFromCallback(int token);

    /** 当前线程是否持有本运行时的 VM Gate。 */
    bool isVmOwnedByCurrentThread() const;

    /** 返回当前 ALPKG；普通 Lua 脚本返回空 shared_ptr。 */
    std::shared_ptr<AlpkgPackage> package() const;

    /** 返回当前正在执行的 ALPKG 文件路径；普通 .lua 脚本返回空字符串。 */
    std::string packagePath() const;

private:
    struct lua_State* state_;
    bool (*shouldInterrupt_)(void*);
    void* controlContext_;
    std::shared_ptr<AlpkgPackage> package_;
    std::unique_ptr<LuaTaskScheduler> scheduler_;

    /** 注册当前 Lua HostApi，包括固定 C ABI 绑定、UI 和 Lua native 多线程入口。 */
    void registerHostApi();

    std::string prepareRun(bool (*shouldInterrupt)(void*), void* controlContext);
    std::string executeLoadedChunk(struct lua_State* executionState, int registryReference);
    struct lua_State* createExecutionState(int* registryReference);
    void installPackageLoaders();

    static int luaPackageRequireSearcher(struct lua_State* state);
    static int luaPackageDofile(struct lua_State* state);
    static int luaPackageLoadfile(struct lua_State* state);
    static int loadPackageChunk(
            struct lua_State* state,
            const std::string& relativePath,
            const char* chunkName,
            std::string* error
    );

    static void controlHook(struct lua_State* state, struct lua_Debug* debug);
};
