/**
 * 文件用途：实现脚本任务状态机，负责运行、停止、暂停和恢复协作。
 */
#include "script_task.h"

ScriptTask::ScriptTask(int taskId)
        : taskId_(taskId),
          status_(ScriptTaskStatus::Idle) {
}

int ScriptTask::taskId() const {
    return taskId_;
}

ScriptTaskStatus ScriptTask::status() const {
    return status_;
}

const std::string& ScriptTask::result() const {
    return result_;
}

const std::string& ScriptTask::error() const {
    return error_;
}

void ScriptTask::markRunning() {
    status_ = ScriptTaskStatus::Running;
    result_.clear();
    error_.clear();
}

void ScriptTask::markFinished(const std::string& result) {
    status_ = ScriptTaskStatus::Finished;
    result_ = result;
    error_.clear();
}

void ScriptTask::markFailed(const std::string& error) {
    status_ = ScriptTaskStatus::Failed;
    result_.clear();
    error_ = error;
}

std::string ScriptTask::statusName() const {
    switch (status_) {
        case ScriptTaskStatus::Idle:
            return "idle";
        case ScriptTaskStatus::Running:
            return "running";
        case ScriptTaskStatus::Pausing:
            return "pausing";
        case ScriptTaskStatus::Paused:
            return "paused";
        case ScriptTaskStatus::Stopping:
            return "stopping";
        case ScriptTaskStatus::Finished:
            return "finished";
        case ScriptTaskStatus::Failed:
            return "failed";
    }
    return "unknown";
}

std::string ScriptTask::summary() const {
    // taskId 和机器状态已经通过 JSON 字段独立返回；这里仅提供可直接展示给用户的中文结果。
    if (status_ == ScriptTaskStatus::Finished) {
        return result_.empty() ? "脚本执行完成" : result_;
    }
    if (status_ == ScriptTaskStatus::Failed) {
        return error_.empty() ? "脚本运行失败" : error_;
    }
    if (status_ == ScriptTaskStatus::Running) {
        return "脚本运行中";
    }
    if (status_ == ScriptTaskStatus::Pausing) {
        return "脚本暂停中";
    }
    if (status_ == ScriptTaskStatus::Paused) {
        return "脚本已暂停";
    }
    if (status_ == ScriptTaskStatus::Stopping) {
        return "脚本停止中";
    }
    return "脚本未启动";
}
