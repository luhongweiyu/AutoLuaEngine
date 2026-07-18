/**
 * 文件用途：声明 Lua 5.4 的全局 imgui 表注册入口。
 */
#pragma once

struct lua_State;

/**
 * 注册懒人精灵兼容的全局 imgui 表。
 *
 * 控件和绘图逻辑全部进入 engine_imgui* C ABI；本层只转换 Lua 参数、保存函数引用并在
 * 持有 VM Gate 时执行回调。
 */
void registerLuaImGuiApi(lua_State* state);
