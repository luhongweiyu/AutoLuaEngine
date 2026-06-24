#include "log_buffer.h"

#include <mutex>
#include <vector>

namespace {

constexpr size_t kMaxLogEntries = 512;

std::mutex gLogMutex;
std::vector<LogEntry> gLogEntries;
int gNextLogId = 1;

} // namespace

void appendLogEntry(const std::string& level, const std::string& message) {
    std::lock_guard<std::mutex> lock(gLogMutex);

    LogEntry entry;
    entry.id = gNextLogId++;
    entry.level = level;
    entry.message = message;
    gLogEntries.push_back(entry);

    if (gLogEntries.size() > kMaxLogEntries) {
        gLogEntries.erase(gLogEntries.begin());
    }
}

std::vector<LogEntry> drainLogEntries(int afterId) {
    std::lock_guard<std::mutex> lock(gLogMutex);

    std::vector<LogEntry> result;
    for (const LogEntry& entry : gLogEntries) {
        if (entry.id > afterId) {
            result.push_back(entry);
        }
    }

    return result;
}
