#pragma once

struct lua_State;

/**
 * 注册 Lua 侧 HostApi。
 *
 * HostApi 是脚本能访问的统一能力入口。第一版先直接注册到 Lua，
 * 后续 JS/Go 会复用同一套 API 语义，但使用各自语言的绑定方式。
 */
void registerHostApi(lua_State* state);
