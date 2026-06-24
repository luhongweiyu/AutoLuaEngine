#pragma once

#include <string>

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
     * @param stopRequested 返回 true 时中断脚本执行。
     * @return 执行成功返回结果摘要，失败返回错误信息。
     */
    std::string runText(const char* code, bool (*stopRequested)(void*), void* stopContext);

private:
    struct lua_State* state_;
    bool (*stopRequested_)(void*);
    void* stopContext_;

    /**
     * 注册第一版 HostApi。
     *
     * 当前包含：
     * - print(...)
     * - sleep(ms)
     */
    void registerHostApi();

    static void stopHook(struct lua_State* state, struct lua_Debug* debug);
};
