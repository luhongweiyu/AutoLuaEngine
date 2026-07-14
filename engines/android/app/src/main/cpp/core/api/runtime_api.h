/**
 * 文件用途：声明脚本运行时通用核心 API，供 Lua/JS/Go 等语言绑定复用。
 */
#pragma once

#include <string>

namespace xiaoyv::api {

/**
 * 脚本停止状态查询函数。
 *
 * 语言绑定层把各自运行时的停止检查包装成这个回调，runtimeSleep 只依赖这个
 * 通用函数指针，不直接依赖 LuaRuntime、JSRuntime 或其他脚本运行时对象。
 */
using ShouldStopCallback = bool (*)(void* context);

/**
 * 顶层脚本停止请求回调。
 *
 * core/api 不依赖具体的 Lua/JS/Go 引擎对象。宿主 Engine 在初始化时注册该回调，
 * exitScript 等跨语言 API 只通过本接口请求停止当前顶层脚本。
 */
using RequestScriptStopCallback = bool (*)(void* context);

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

/** 注册当前宿主的顶层脚本停止入口。 */
void runtimeSetScriptStopRequester(RequestScriptStopCallback callback, void* context);

/**
 * 请求停止当前顶层脚本。
 *
 * 返回 true 表示宿主接受了请求；没有运行中脚本或宿主未初始化时返回 false。
 */
bool runtimeRequestScriptStop();

/**
 * 记录当前顶层脚本任务开始时间。
 *
 * 同一个脚本创建的 Lua native 子线程必须共享该起点，因此这里保存的是引擎当前
 * 顶层任务时间，不使用线程局部数据。Engine 当前只允许一个顶层脚本同时运行。
 */
void runtimeMarkScriptStart();

/**
 * 设置当前顶层脚本的工作目录。
 *
 * Engine 在每次脚本任务开始前写入真实脚本文件或 ALPKG 所在目录；脚本结束后清空，避免
 * 下一次任务误读上一份脚本路径。
 */
void runtimeSetScriptWorkPath(const std::string& path);

/** 返回当前顶层脚本工作目录；当前没有运行脚本时返回空字符串。 */
std::string runtimeScriptWorkPath();

/**
 * 返回系统时间戳，单位毫秒。
 *
 * 语义等同 Unix epoch 毫秒时间戳，用于脚本记录真实时间。
 */
long long runtimeSystemTimeMs();

/**
 * 返回当前脚本已运行时间，单位毫秒。
 *
 * 如果调用线程还没有记录脚本开始时间，则从第一次调用时开始计时。
 */
long long runtimeTickCountMs();

} // namespace xiaoyv::api
