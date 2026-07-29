/**
 * 文件用途：注册静态链接的 LuaSocket C 核心模块。
 */
#include "luasocket_lua_api.h"

extern "C" {
#include "lua.h"

int luaopen_socket_core(lua_State* state);
int luaopen_mime_core(lua_State* state);
}

namespace {

void registerPreloadModule(lua_State* state, const char* moduleName, lua_CFunction loader) {
    lua_getglobal(state, "package");
    if (!lua_istable(state, -1)) {
        lua_pop(state, 1);
        return;
    }

    lua_getfield(state, -1, "preload");
    if (lua_istable(state, -1)) {
        lua_pushcfunction(state, loader);
        lua_setfield(state, -2, moduleName);
    }
    lua_pop(state, 2);
}

} // namespace

void registerLuaSocketNativeModules(lua_State* state) {
    if (state == nullptr) {
        return;
    }

    registerPreloadModule(state, "socket.core", luaopen_socket_core);
    registerPreloadModule(state, "mime.core", luaopen_mime_core);
}
