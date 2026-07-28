/**
 * 文件用途：声明 OpenCV 扩展中 Point 和基础标量指针的 Lua userdata 兼容绑定。
 */
#pragma once

struct lua_State;

void registerCvCompatLuaApi(lua_State* state, int hostTableIndex);
