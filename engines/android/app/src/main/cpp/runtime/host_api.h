#pragma once

struct lua_State;

/**
 * 注册 Lua 侧 HostApi。
 *
 * C++ 这里只注册 native `_host` 表。脚本用户看到的 `m/lr/cd` 命名空间
 * 由 assets/runtime 下的 Lua 文件封装，避免兼容层逻辑散进 native 代码。
 */
void registerHostApi(lua_State* state);
