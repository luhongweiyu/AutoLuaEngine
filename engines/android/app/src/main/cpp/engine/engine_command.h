#pragma once

#include <string>

class Engine;

/**
 * native 统一命令入口。
 *
 * App、悬浮窗、IDE、后续 JS/插件都只应该把命令送到这里。这里负责参数校验、
 * 任务控制、系统 API 调度和 JSON 结果生成；Java 只保留 HTTP/进程/Android
 * 框架桥这些必须由 Android 层完成的工作。
 */
std::string handleEngineCommand(
        Engine& engine,
        const std::string& method,
        const std::string& paramsJson,
        const std::string& luaRuntimeBootstrap);
