/**
 * 文件用途：声明 Lua 5.4 的受限原生动态库调用兼容层。
 */
#pragma once

struct lua_State;

void registerFfiLuaApi(lua_State* state, int hostTableIndex);
