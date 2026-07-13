/**
 * 文件用途：声明 LuaRuntime，封装单个 Lua 虚拟机和脚本执行入口。
 */
#pragma once

#include <memory>
#include <string>

class AlpkgPackage;

/**
 * LuaRuntime 负责管理单个 Lua 虚拟机。
 *
 * 第一版只支持同步执行一段 Lua 文本，用于验证 Lua 引擎接入链路。
 * 后续 ScriptTask、多任务、取消执行会在这个类之上继续扩展。
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
    bool shouldInterruptNow() const;

    /** 返回当前正在执行的 ALPKG 文件路径；普通 .lua 脚本返回空字符串。 */
    std::string packagePath() const;

private:
    struct lua_State* state_;
    bool (*shouldInterrupt_)(void*);
    void* controlContext_;
    std::shared_ptr<AlpkgPackage> package_;

    /**
     * 注册第一版 HostApi。
     *
     * 当前包含：
     * - print(...)
     * - sleep(ms)
     */
    void registerHostApi();

    std::string prepareRun(bool (*shouldInterrupt)(void*), void* controlContext);
    std::string executeLoadedChunk();
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
