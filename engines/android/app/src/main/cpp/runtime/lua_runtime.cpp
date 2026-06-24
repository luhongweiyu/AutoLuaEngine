#include "lua_runtime.h"

#include <string>

#include "host_api.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

LuaRuntime::LuaRuntime()
        : state_(luaL_newstate()),
          stopRequested_(nullptr),
          stopContext_(nullptr) {
    if (state_ == nullptr) {
        return;
    }

    luaL_openlibs(state_);
    registerHostApi();
}

LuaRuntime::~LuaRuntime() {
    if (state_ != nullptr) {
        lua_close(state_);
        state_ = nullptr;
    }
}

std::string LuaRuntime::runText(const char* code, bool (*stopRequested)(void*), void* stopContext) {
    if (state_ == nullptr) {
        return "LuaRuntime init failed";
    }

    stopRequested_ = stopRequested;
    stopContext_ = stopContext;
    lua_pushlightuserdata(state_, this);
    lua_setfield(state_, LUA_REGISTRYINDEX, "AutoLuaEngineRuntime");

    // 每执行一批 VM 指令检查一次停止请求。这个 hook 只用于协作取消，
    // 不强杀线程，能让 Lua 栈按错误路径正常展开。
    lua_sethook(state_, LuaRuntime::stopHook, LUA_MASKCOUNT, 1000);

    // luaL_loadstring 只负责编译，lua_pcall 才实际执行。
    // 分开处理可以给出更明确的错误来源。
    int loadStatus = luaL_loadstring(state_, code);
    if (loadStatus != LUA_OK) {
        const char* error = lua_tostring(state_, -1);
        std::string message = error == nullptr ? "Lua load error" : error;
        lua_pop(state_, 1);
        return "Lua load failed: " + message;
    }

    int callStatus = lua_pcall(state_, 0, LUA_MULTRET, 0);
    if (callStatus != LUA_OK) {
        const char* error = lua_tostring(state_, -1);
        std::string message = error == nullptr ? "Lua runtime error" : error;
        lua_pop(state_, 1);
        return "Lua run failed: " + message;
    }

    return "Lua script OK";
}

void LuaRuntime::registerHostApi() {
    ::registerHostApi(state_);
}

void LuaRuntime::stopHook(lua_State* state, lua_Debug* debug) {
    (void) debug;

    lua_getfield(state, LUA_REGISTRYINDEX, "AutoLuaEngineRuntime");
    LuaRuntime* runtime = static_cast<LuaRuntime*>(lua_touserdata(state, -1));
    lua_pop(state, 1);

    if (runtime == nullptr || runtime->stopRequested_ == nullptr) {
        return;
    }

    if (runtime->stopRequested_(runtime->stopContext_)) {
        luaL_error(state, "script stopped");
    }
}
