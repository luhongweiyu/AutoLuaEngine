/**
 * 文件用途：声明懒人精灵兼容找色 Lua 辅助绑定。
 */
#pragma once

struct lua_State;

void registerColorCompatLuaApi(lua_State* state, int hostTableIndex);
