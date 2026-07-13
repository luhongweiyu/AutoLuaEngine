/**
 * 文件用途：声明 Lua 多线程脚本 API 注册入口。
 */
#pragma once

struct lua_State;

/**
 * 向 `_host.thread` 注册 beginThread、newThread，并注册线程对象的 stopThread 元方法。
 */
void registerLuaThreadApi(lua_State* state, int hostTableIndex);
