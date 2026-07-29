/**
 * 文件用途：声明内置 cffi-lua 模块到 Lua HostApi 的注册入口。
 */
#pragma once

struct lua_State;

void registerFfiLuaApi(lua_State* state, int hostTableIndex);
