/**
 * 文件用途：声明 native 日志缓冲入口，统一保存脚本和引擎日志。
 */
#pragma once

#include <string>
#include <vector>

struct LogEntry {
    int id = 0;
    std::string level;
    std::string message;
};

/**
 * Native 日志缓冲。
 *
 * 第一版用于 HTTP `log.drain` 轮询读取脚本日志。这里保持为简单环形缓冲，
 * 避免日志无限增长；后续做 WebSocket 实时推送时仍可复用这个入口。
 */
void appendLogEntry(const std::string& level, const std::string& message);

std::vector<LogEntry> drainLogEntries(int afterId);
