#pragma once

#include <string>

/**
 * 脚本任务状态。
 *
 * 第一版先同步执行任务，但状态模型提前建立，后续异步线程、停止任务和 IDE
 * 查询任务状态都沿用这套枚举。
 */
enum class ScriptTaskStatus {
    Idle,
    Running,
    Finished,
    Failed
};

/**
 * 单次脚本执行任务。
 *
 * 当前只承载一次 Lua 文本执行结果；后续会扩展 language、startTime、
 * stopRequested、日志关联等字段。
 */
class ScriptTask {
public:
    explicit ScriptTask(int taskId);

    int taskId() const;
    ScriptTaskStatus status() const;
    const std::string& result() const;
    const std::string& error() const;

    void markRunning();
    void markFinished(const std::string& result);
    void markFailed(const std::string& error);

    std::string statusName() const;
    std::string summary() const;

private:
    int taskId_;
    ScriptTaskStatus status_;
    std::string result_;
    std::string error_;
};
