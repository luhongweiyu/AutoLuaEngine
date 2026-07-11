/**
 * 文件用途：声明引擎命令分发入口，统一处理方法名和 JSON 参数。
 */
#pragma once

#include <string>

class Engine;

/**
 * native 统一命令入口。
 *
 * App、悬浮窗、IDE 和后续控制端插件都只应该把控制命令送到这里。这里负责
 * 参数校验、任务控制、状态查询和 JSON 结果生成；Java 只保留 HTTP/进程/Android
 * 框架桥这些必须由 Android 层完成的工作。
 */
std::string handleEngineCommand(
        Engine& engine,
        const std::string& method,
        const std::string& paramsJson,
        const std::string& luaRuntimeBootstrap);
