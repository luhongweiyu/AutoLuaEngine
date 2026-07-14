/**
 * 文件用途：实现 Lua Thread.newThread、beginThread 和 thread:stopThread 参数绑定。
 */
#include "lua_thread_api.h"

#include <string>

#include "lua_runtime.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

namespace {

constexpr const char* kThreadMetatable = "小鱼精灵.Thread";

/**
 * Lua 线程对象只保存运行时地址和任务 ID，不持有 native thread 所有权。
 * LuaRuntime 在主脚本结束时统一停止并 join 全部线程。
 */
struct LuaThreadUserdata {
    LuaRuntime* runtime = nullptr;
    long long taskId = 0;
};

LuaRuntime* checkRuntime(lua_State* state) {
    LuaRuntime* runtime = LuaRuntime::fromState(state);
    if (runtime == nullptr) {
        luaL_error(state, "Lua 多线程运行时不可用");
        return nullptr;
    }
    return runtime;
}

long long startThread(lua_State* state) {
    LuaRuntime* runtime = checkRuntime(state);
    luaL_checktype(state, 1, LUA_TFUNCTION);

    const int argumentCount = lua_gettop(state) - 1;
    std::string error;
    long long taskId = runtime->startChildThread(state, 1, argumentCount, &error);
    if (taskId <= 0) {
        luaL_error(state, "%s", error.empty() ? "创建子线程失败" : error.c_str());
        return 0;
    }
    return taskId;
}

int luaBeginThread(lua_State* state) {
    startThread(state);
    return 0;
}

int luaNewThread(lua_State* state) {
    LuaRuntime* runtime = checkRuntime(state);
    long long taskId = startThread(state);

    void* memory = lua_newuserdatauv(state, sizeof(LuaThreadUserdata), 0);
    auto* userdata = static_cast<LuaThreadUserdata*>(memory);
    userdata->runtime = runtime;
    userdata->taskId = taskId;
    luaL_setmetatable(state, kThreadMetatable);
    return 1;
}

int luaStopThread(lua_State* state) {
    auto* userdata = static_cast<LuaThreadUserdata*>(
            luaL_checkudata(state, 1, kThreadMetatable)
    );
    if (userdata->runtime == nullptr || userdata->taskId <= 0) {
        return 0;
    }

    std::string error;
    if (!userdata->runtime->stopChildThread(userdata->taskId, &error)) {
        // 子线程停止自己时直接抛出 Lua 错误，使该子任务在当前调用点结束。
        return luaL_error(
                state,
                "%s",
                error.empty() ? "当前线程已停止" : error.c_str()
        );
    }
    return 0;
}

int luaThreadToString(lua_State* state) {
    auto* userdata = static_cast<LuaThreadUserdata*>(
            luaL_checkudata(state, 1, kThreadMetatable)
    );
    lua_pushfstring(state, "Thread(%I)", static_cast<lua_Integer>(userdata->taskId));
    return 1;
}

void setFunction(lua_State* state, int tableIndex, const char* name, lua_CFunction function) {
    int absoluteTableIndex = lua_absindex(state, tableIndex);
    lua_pushcfunction(state, function);
    lua_setfield(state, absoluteTableIndex, name);
}

} // namespace

void registerLuaThreadApi(lua_State* state, int hostTableIndex) {
    if (luaL_newmetatable(state, kThreadMetatable) != 0) {
        lua_newtable(state);
        setFunction(state, -1, "stopThread", luaStopThread);
        lua_setfield(state, -2, "__index");
        setFunction(state, -1, "__tostring", luaThreadToString);
    }
    lua_pop(state, 1);

    lua_newtable(state);
    int threadTableIndex = lua_gettop(state);
    setFunction(state, threadTableIndex, "beginThread", luaBeginThread);
    setFunction(state, threadTableIndex, "newThread", luaNewThread);
    lua_setfield(state, hostTableIndex, "thread");
}
