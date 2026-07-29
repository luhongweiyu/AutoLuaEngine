/**
 * 文件用途：把静态编入引擎的 cffi-lua 模块挂接到 _host.ffi。
 */
#include "ffi_lua_api.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"

int luaopen_cffi(lua_State* state);
}

void registerFfiLuaApi(lua_State* state, int hostTableIndex) {
    const int hostIndex = lua_absindex(state, hostTableIndex);
    const int stackTop = lua_gettop(state);
    const int resultCount = luaopen_cffi(state);
    if (resultCount != 1 || lua_gettop(state) != stackTop + 1 || !lua_istable(state, -1)) {
        lua_settop(state, stackTop);
        luaL_error(state, "内置 ffi 模块初始化失败");
        return;
    }

    // 公开入口仍然是 m.ffi、全局 ffi 与 require("ffi")；cffi 只是上游模块名。
    lua_setfield(state, hostIndex, "ffi");
}
