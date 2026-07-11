/**
 * 文件用途：声明脚本运行时通用核心 API，供 Lua/JS/Go 等语言绑定复用。
 */
#pragma once

#include <string>

namespace autolua::api {

/**
 * 脚本停止状态查询函数。
 *
 * 语言绑定层把各自运行时的停止检查包装成这个回调，runtimeSleep 只依赖这个
 * 通用函数指针，不直接依赖 LuaRuntime、JSRuntime 或其他脚本运行时对象。
 */
using ShouldStopCallback = bool (*)(void* context);

/**
 * 写入 info 级脚本日志。
 *
 * 当前会同时写入 Android logcat 和 native 日志缓冲。print、log.print、后续
 * JS/Go 的 console/log 都应该复用这里，不在各语言绑定里重复实现日志输出。
 */
void runtimePrint(const std::string& message);

/**
 * runtimePrint 的语义化别名。
 *
 * 保留独立函数是为了后续区分 print 输出和日志模块输出时不改语言绑定层。
 */
void runtimeLogPrint(const std::string& message);

/**
 * 脚本延时。
 *
 * 返回 true 表示正常睡眠完成；返回 false 表示 shouldStop 回调要求中断脚本。
 * 这个函数不抛异常，语言绑定层负责把 false 转成各自语言的错误或返回值。
 */
bool runtimeSleep(long long durationMs, ShouldStopCallback shouldStop, void* stopContext);

} // namespace autolua::api
