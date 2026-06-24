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
        case ScriptTaskStatus::Finished:
            return "finished";
        case ScriptTaskStatus::Failed:
            return "failed";
    }
    return "unknown";
}

std::string ScriptTask::summary() const {
    std::string text = "task#";
    text += std::to_string(taskId_);
    text += " ";
    text += statusName();

    if (status_ == ScriptTaskStatus::Finished) {
        text += ": ";
        text += result_;
    } else if (status_ == ScriptTaskStatus::Failed) {
        text += ": ";
        text += error_;
    }

    return text;
}
