/**
 * 文件用途：把静态编入 libengine.so 的 LuaSocket 原生核心注册为 Lua 模块加载器。
 */
#pragma once

struct lua_State;

/**
 * 注册 socket.core 与 mime.core 的 package.preload 加载器。
 *
 * LuaSocket 的上层 Lua 模块由 Android assets 的运行时引导注册；这里仅连接它们依赖的
 * C 核心，不新增小鱼专有脚本 API。
 */
void registerLuaSocketNativeModules(lua_State* state);
