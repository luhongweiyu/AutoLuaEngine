#include "host_api.h"

#include <android/log.h>
#include <chrono>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <cstdlib>

#include "../engine/engine_config.h"
#include "../platform/android_bridge.h"
#include "image_store.h"
#include "log_buffer.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

namespace {

constexpr const char* kLogTag = "AutoLuaEngine";

void logInfo(const std::string& message) {
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", message.c_str());
    appendLogEntry("info", message);
}

/**
 * 将 Lua 栈上的任意值转换为可读字符串。
 *
 * print(...) 允许传入任意类型。这里使用 luaL_tolstring 复用 Lua 自己的
 * __tostring 规则，避免手写大量类型判断。
 */
std::string luaValueToString(lua_State* state, int index) {
    luaL_tolstring(state, index, nullptr);
    const char* text = lua_tostring(state, -1);
    std::string result = text == nullptr ? "" : text;
    lua_pop(state, 1);
    return result;
}

int luaPrint(lua_State* state) {
    int count = lua_gettop(state);
    std::ostringstream output;

    for (int i = 1; i <= count; ++i) {
        if (i > 1) {
            output << "\t";
        }
        output << luaValueToString(state, i);
    }

    logInfo(output.str());
    return 0;
}

int luaSleep(lua_State* state) {
    lua_Integer duration = luaL_checkinteger(state, 1);
    if (duration < 0) {
        return luaL_error(state, "sleep duration must be greater than or equal to 0");
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(duration));
    lua_pushboolean(state, 1);
    return 1;
}

int luaLogPrint(lua_State* state) {
    const char* text = luaL_checkstring(state, 1);
    logInfo(text == nullptr ? "" : text);
    lua_pushboolean(state, 1);
    return 1;
}

int luaDeviceInfo(lua_State* state) {
    lua_newtable(state);

    lua_pushstring(state, "android");
    lua_setfield(state, -2, "platform");

    lua_pushstring(state, EngineConfig::kEngineVersion);
    lua_setfield(state, -2, "engineVersion");

    lua_pushstring(state, LUA_VERSION);
    lua_setfield(state, -2, "luaVersion");

    bool rootModeEnabled = AndroidBridge::isRootModeEnabled();
    bool rootAvailable = AndroidBridge::isRootAvailable();
    bool accessibilityEnabled = AndroidBridge::isAccessibilityEnabled();
    lua_pushboolean(state, rootModeEnabled ? 1 : 0);
    lua_setfield(state, -2, "rootModeEnabled");
    lua_pushboolean(state, rootAvailable ? 1 : 0);
    lua_setfield(state, -2, "rootAvailable");

    lua_pushboolean(state, accessibilityEnabled ? 1 : 0);
    lua_setfield(state, -2, "accessibilityEnabled");

    const char* automationMode = "none";
    if (rootModeEnabled && rootAvailable) {
        automationMode = "root-first";
    } else if (accessibilityEnabled) {
        automationMode = "accessibility";
    }
    lua_pushstring(state, automationMode);
    lua_setfield(state, -2, "automationMode");

    return 1;
}

int luaDeviceIsRootAvailable(lua_State* state) {
    lua_pushboolean(state, AndroidBridge::isRootAvailable() ? 1 : 0);
    return 1;
}

int luaDeviceSetRootModeEnabled(lua_State* state) {
    luaL_checktype(state, 1, LUA_TBOOLEAN);
    bool enabled = lua_toboolean(state, 1) != 0;
    if (!AndroidBridge::setRootModeEnabled(enabled)) {
        lua_pushnil(state);
        lua_pushstring(state, "set root mode failed");
        return 2;
    }

    lua_pushboolean(state, 1);
    return 1;
}

int luaRootExec(lua_State* state) {
    const char* command = luaL_checkstring(state, 1);
    lua_Integer timeoutMs = luaL_optinteger(state, 2, 2500);
    if (timeoutMs < 0) {
        return luaL_error(state, "root exec timeout must be greater than or equal to 0");
    }

    RootExecResult result = AndroidBridge::rootExec(command, static_cast<int>(timeoutMs));
    lua_newtable(state);
    lua_pushboolean(state, result.success ? 1 : 0);
    lua_setfield(state, -2, "ok");
    lua_pushinteger(state, result.exitCode);
    lua_setfield(state, -2, "exitCode");
    lua_pushstring(state, result.stdoutText.c_str());
    lua_setfield(state, -2, "stdout");
    lua_pushstring(state, result.stderrText.c_str());
    lua_setfield(state, -2, "stderr");
    lua_pushboolean(state, result.timedOut ? 1 : 0);
    lua_setfield(state, -2, "timedOut");
    lua_pushstring(state, result.error.c_str());
    lua_setfield(state, -2, "error");
    return 1;
}

int luaRootStatus(lua_State* state) {
    RootStatusResult status = AndroidBridge::rootStatus();

    lua_newtable(state);
    lua_pushboolean(state, status.available ? 1 : 0);
    lua_setfield(state, -2, "available");
    lua_pushstring(state, status.commandMode.c_str());
    lua_setfield(state, -2, "commandMode");
    lua_pushstring(state, status.suPath.c_str());
    lua_setfield(state, -2, "suPath");
    lua_pushboolean(state, status.cached ? 1 : 0);
    lua_setfield(state, -2, "cached");
    lua_pushinteger(state, static_cast<lua_Integer>(status.cacheExpireAt));
    lua_setfield(state, -2, "cacheExpireAt");
    lua_pushstring(state, status.error.c_str());
    lua_setfield(state, -2, "error");

    lua_createtable(state, static_cast<int>(status.attempts.size()), 0);
    for (size_t i = 0; i < status.attempts.size(); ++i) {
        const RootProbeAttempt& attempt = status.attempts[i];
        lua_newtable(state);
        lua_pushstring(state, attempt.commandMode.c_str());
        lua_setfield(state, -2, "commandMode");
        lua_pushstring(state, attempt.suPath.c_str());
        lua_setfield(state, -2, "suPath");
        lua_pushinteger(state, attempt.exitCode);
        lua_setfield(state, -2, "exitCode");
        lua_pushstring(state, attempt.stdoutText.c_str());
        lua_setfield(state, -2, "stdout");
        lua_pushstring(state, attempt.stderrText.c_str());
        lua_setfield(state, -2, "stderr");
        lua_pushboolean(state, attempt.timedOut ? 1 : 0);
        lua_setfield(state, -2, "timedOut");
        lua_pushstring(state, attempt.error.c_str());
        lua_setfield(state, -2, "error");
        lua_rawseti(state, -2, static_cast<lua_Integer>(i + 1));
    }
    lua_setfield(state, -2, "attempts");

    return 1;
}

std::string rootResultError(const RootExecResult& result, const char* fallback) {
    if (!result.error.empty()) {
        return result.error;
    }
    if (!result.stderrText.empty()) {
        return result.stderrText;
    }
    return fallback;
}

std::string trimKeyValueText(const std::string& value) {
    size_t start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }

    size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

void pushTypedKeyValue(lua_State* state, const std::string& value) {
    if (value == "true" || value == "false") {
        lua_pushboolean(state, value == "true" ? 1 : 0);
        return;
    }

    char* integerEnd = nullptr;
    long long integerValue = std::strtoll(value.c_str(), &integerEnd, 10);
    if (integerEnd != value.c_str() && *integerEnd == '\0') {
        lua_pushinteger(state, static_cast<lua_Integer>(integerValue));
        return;
    }

    char* numberEnd = nullptr;
    double numberValue = std::strtod(value.c_str(), &numberEnd);
    if (numberEnd != value.c_str() && *numberEnd == '\0') {
        lua_pushnumber(state, static_cast<lua_Number>(numberValue));
        return;
    }

    lua_pushstring(state, value.c_str());
}

void pushKeyValueTable(lua_State* state, const std::string& text) {
    lua_newtable(state);
    int tableIndex = lua_gettop(state);
    std::istringstream stream(text);
    std::string line;

    while (std::getline(stream, line)) {
        size_t separator = line.find('=');
        if (separator == std::string::npos || separator == 0) {
            continue;
        }

        std::string key = trimKeyValueText(line.substr(0, separator));
        std::string value = trimKeyValueText(line.substr(separator + 1));
        if (key.empty()) {
            continue;
        }

        pushTypedKeyValue(state, value);
        lua_setfield(state, tableIndex, key.c_str());
    }
}

int pushRootKeyValueResult(lua_State* state,
                           const RootExecResult& result,
                           const char* fallbackError) {
    if (!result.success) {
        std::string error = rootResultError(result, fallbackError);
        lua_pushnil(state);
        lua_pushstring(state, error.c_str());
        return 2;
    }

    pushKeyValueTable(state, result.stdoutText);
    return 1;
}

int pushRootBooleanResult(lua_State* state,
                          const RootExecResult& result,
                          const char* fallbackError) {
    if (!result.success) {
        std::string error = rootResultError(result, fallbackError);
        lua_pushnil(state);
        lua_pushstring(state, error.c_str());
        return 2;
    }

    lua_pushboolean(state, 1);
    return 1;
}

int luaDeviceScreenState(lua_State* state) {
    RootExecResult result = AndroidBridge::deviceScreenState();
    return pushRootKeyValueResult(state, result, "device screen state failed");
}

int luaDeviceWake(lua_State* state) {
    RootExecResult result = AndroidBridge::deviceWake();
    return pushRootBooleanResult(state, result, "device wake failed");
}

int luaDeviceSleep(lua_State* state) {
    RootExecResult result = AndroidBridge::deviceSleep();
    return pushRootBooleanResult(state, result, "device sleep failed");
}

int luaDeviceBattery(lua_State* state) {
    RootExecResult result = AndroidBridge::deviceBattery();
    return pushRootKeyValueResult(state, result, "device battery failed");
}

int luaDeviceRotation(lua_State* state) {
    RootExecResult result = AndroidBridge::deviceRotation();
    return pushRootKeyValueResult(state, result, "device rotation failed");
}

int luaDeviceSetRotation(lua_State* state) {
    lua_Integer rotation = luaL_checkinteger(state, 1);
    bool locked = lua_isnoneornil(state, 2) || lua_toboolean(state, 2) != 0;
    RootExecResult result = AndroidBridge::deviceSetRotation(static_cast<int>(rotation), locked);
    return pushRootBooleanResult(state, result, "device set rotation failed");
}

int luaRootFileExists(lua_State* state) {
    const char* path = luaL_checkstring(state, 1);
    RootExecResult result = AndroidBridge::rootFileExists(path);
    lua_pushboolean(state, result.success ? 1 : 0);
    return 1;
}

int luaRootFileReadText(lua_State* state) {
    const char* path = luaL_checkstring(state, 1);
    lua_Integer timeoutMs = luaL_optinteger(state, 2, 2500);
    if (timeoutMs < 0) {
        return luaL_error(state, "root file read timeout must be greater than or equal to 0");
    }

    RootExecResult result = AndroidBridge::rootFileReadText(path, static_cast<int>(timeoutMs));
    if (!result.success) {
        std::string error = rootResultError(result, "root file read failed");
        lua_pushnil(state);
        lua_pushstring(state, error.c_str());
        return 2;
    }

    lua_pushlstring(state, result.stdoutText.data(), result.stdoutText.size());
    return 1;
}

int luaRootFileWriteText(lua_State* state) {
    const char* path = luaL_checkstring(state, 1);
    size_t contentLength = 0;
    const char* content = luaL_checklstring(state, 2, &contentLength);
    lua_Integer timeoutMs = luaL_optinteger(state, 3, 2500);
    if (timeoutMs < 0) {
        return luaL_error(state, "root file write timeout must be greater than or equal to 0");
    }

    RootExecResult result = AndroidBridge::rootFileWriteText(
            path,
            std::string(content, contentLength),
            static_cast<int>(timeoutMs)
    );
    if (!result.success) {
        std::string error = rootResultError(result, "root file write failed");
        lua_pushnil(state);
        lua_pushstring(state, error.c_str());
        return 2;
    }

    lua_pushboolean(state, 1);
    return 1;
}

struct RootFileInfo {
    std::string type;
    long long size = 0;
    std::string mode;
    std::string user;
    std::string group;
    int uid = 0;
    int gid = 0;
    long long modifiedAt = 0;
    std::string path;
    std::string name;
};

std::string baseName(const std::string& path) {
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return path;
    }
    if (pos + 1 >= path.size()) {
        return "";
    }
    return path.substr(pos + 1);
}

std::vector<std::string> splitStatLine(const std::string& line) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (parts.size() < 8) {
        size_t pos = line.find('|', start);
        if (pos == std::string::npos) {
            return {};
        }
        parts.push_back(line.substr(start, pos - start));
        start = pos + 1;
    }
    parts.push_back(line.substr(start));
    return parts;
}

bool parseRootFileInfoLine(const std::string& line, RootFileInfo* info) {
    if (info == nullptr || line.empty()) {
        return false;
    }

    std::string cleanLine = line;
    while (!cleanLine.empty() && (cleanLine.back() == '\n' || cleanLine.back() == '\r')) {
        cleanLine.pop_back();
    }

    std::vector<std::string> parts = splitStatLine(cleanLine);
    if (parts.size() != 9) {
        return false;
    }

    char* sizeEnd = nullptr;
    char* uidEnd = nullptr;
    char* gidEnd = nullptr;
    char* modifiedEnd = nullptr;
    long long size = std::strtoll(parts[1].c_str(), &sizeEnd, 10);
    long uid = std::strtol(parts[5].c_str(), &uidEnd, 10);
    long gid = std::strtol(parts[6].c_str(), &gidEnd, 10);
    long long modifiedAt = std::strtoll(parts[7].c_str(), &modifiedEnd, 10);
    if (sizeEnd == parts[1].c_str()
            || *sizeEnd != '\0'
            || uidEnd == parts[5].c_str()
            || *uidEnd != '\0'
            || gidEnd == parts[6].c_str()
            || *gidEnd != '\0'
            || modifiedEnd == parts[7].c_str()
            || *modifiedEnd != '\0') {
        return false;
    }

    info->type = parts[0];
    info->size = size;
    info->mode = parts[2];
    info->user = parts[3];
    info->group = parts[4];
    info->uid = static_cast<int>(uid);
    info->gid = static_cast<int>(gid);
    info->modifiedAt = modifiedAt;
    info->path = parts[8];
    info->name = baseName(info->path);
    return true;
}

void pushRootFileInfo(lua_State* state, const RootFileInfo& info) {
    lua_newtable(state);
    lua_pushstring(state, info.type.c_str());
    lua_setfield(state, -2, "type");
    lua_pushinteger(state, static_cast<lua_Integer>(info.size));
    lua_setfield(state, -2, "size");
    lua_pushstring(state, info.mode.c_str());
    lua_setfield(state, -2, "mode");
    lua_pushstring(state, info.user.c_str());
    lua_setfield(state, -2, "user");
    lua_pushstring(state, info.group.c_str());
    lua_setfield(state, -2, "group");
    lua_pushinteger(state, info.uid);
    lua_setfield(state, -2, "uid");
    lua_pushinteger(state, info.gid);
    lua_setfield(state, -2, "gid");
    lua_pushinteger(state, static_cast<lua_Integer>(info.modifiedAt));
    lua_setfield(state, -2, "modifiedAt");
    lua_pushstring(state, info.path.c_str());
    lua_setfield(state, -2, "path");
    lua_pushstring(state, info.name.c_str());
    lua_setfield(state, -2, "name");
}

int luaRootFileStat(lua_State* state) {
    const char* path = luaL_checkstring(state, 1);
    RootExecResult result = AndroidBridge::rootFileStat(path);
    if (!result.success) {
        std::string error = rootResultError(result, "root file stat failed");
        lua_pushnil(state);
        lua_pushstring(state, error.c_str());
        return 2;
    }

    RootFileInfo info;
    if (!parseRootFileInfoLine(result.stdoutText, &info)) {
        lua_pushnil(state);
        lua_pushstring(state, "root file stat output is invalid");
        return 2;
    }

    pushRootFileInfo(state, info);
    return 1;
}

int luaRootFileList(lua_State* state) {
    const char* path = luaL_checkstring(state, 1);
    RootExecResult result = AndroidBridge::rootFileList(path);
    if (!result.success) {
        std::string error = rootResultError(result, "root file list failed");
        lua_pushnil(state);
        lua_pushstring(state, error.c_str());
        return 2;
    }

    std::vector<RootFileInfo> entries;
    std::istringstream stream(result.stdoutText);
    std::string line;
    while (std::getline(stream, line)) {
        RootFileInfo info;
        if (parseRootFileInfoLine(line, &info)) {
            entries.push_back(info);
        }
    }

    lua_createtable(state, static_cast<int>(entries.size()), 0);
    for (size_t i = 0; i < entries.size(); ++i) {
        pushRootFileInfo(state, entries[i]);
        lua_rawseti(state, -2, static_cast<lua_Integer>(i + 1));
    }
    return 1;
}

int luaRootFileRemove(lua_State* state) {
    const char* path = luaL_checkstring(state, 1);
    bool recursive = !lua_isnoneornil(state, 2) && lua_toboolean(state, 2) != 0;
    RootExecResult result = AndroidBridge::rootFileRemove(path, recursive);
    if (!result.success) {
        std::string error = rootResultError(result, "root file remove failed");
        lua_pushnil(state);
        lua_pushstring(state, error.c_str());
        return 2;
    }

    lua_pushboolean(state, 1);
    return 1;
}

int luaRootFileMkdir(lua_State* state) {
    const char* path = luaL_checkstring(state, 1);
    bool recursive = lua_isnoneornil(state, 2) || lua_toboolean(state, 2) != 0;
    RootExecResult result = AndroidBridge::rootFileMkdir(path, recursive);
    if (!result.success) {
        std::string error = rootResultError(result, "root file mkdir failed");
        lua_pushnil(state);
        lua_pushstring(state, error.c_str());
        return 2;
    }

    lua_pushboolean(state, 1);
    return 1;
}

int luaRootFileChmod(lua_State* state) {
    const char* path = luaL_checkstring(state, 1);
    const char* mode = luaL_checkstring(state, 2);
    RootExecResult result = AndroidBridge::rootFileChmod(path, mode);
    if (!result.success) {
        std::string error = rootResultError(result, "root file chmod failed");
        lua_pushnil(state);
        lua_pushstring(state, error.c_str());
        return 2;
    }

    lua_pushboolean(state, 1);
    return 1;
}

int luaRootFileChown(lua_State* state) {
    const char* path = luaL_checkstring(state, 1);
    const char* owner = luaL_checkstring(state, 2);
    RootExecResult result = AndroidBridge::rootFileChown(path, owner);
    if (!result.success) {
        std::string error = rootResultError(result, "root file chown failed");
        lua_pushnil(state);
        lua_pushstring(state, error.c_str());
        return 2;
    }

    lua_pushboolean(state, 1);
    return 1;
}

std::vector<int> parsePidList(const std::string& text) {
    std::vector<int> pids;
    std::istringstream stream(text);
    std::string part;
    while (stream >> part) {
        char* end = nullptr;
        long value = std::strtol(part.c_str(), &end, 10);
        if (end != part.c_str() && *end == '\0' && value > 0) {
            pids.push_back(static_cast<int>(value));
        }
    }
    return pids;
}

struct ProcessInfo {
    int pid = 0;
    int ppid = 0;
    std::string user;
    std::string name;
    std::string args;
};

bool parseProcessLine(const std::string& line, ProcessInfo* process) {
    if (process == nullptr || line.empty()) {
        return false;
    }

    std::istringstream stream(line);
    std::string pidText;
    std::string ppidText;
    std::string user;
    std::string name;
    if (!(stream >> pidText >> ppidText >> user >> name)) {
        return false;
    }

    char* pidEnd = nullptr;
    char* ppidEnd = nullptr;
    long pid = std::strtol(pidText.c_str(), &pidEnd, 10);
    long ppid = std::strtol(ppidText.c_str(), &ppidEnd, 10);
    if (pidEnd == pidText.c_str()
            || *pidEnd != '\0'
            || ppidEnd == ppidText.c_str()
            || *ppidEnd != '\0'
            || pid <= 0
            || ppid < 0) {
        return false;
    }

    std::string args;
    std::getline(stream, args);
    size_t firstVisible = args.find_first_not_of(" \t");
    if (firstVisible != std::string::npos) {
        args = args.substr(firstVisible);
    } else {
        args.clear();
    }

    process->pid = static_cast<int>(pid);
    process->ppid = static_cast<int>(ppid);
    process->user = user;
    process->name = name;
    process->args = args;
    return true;
}

std::vector<ProcessInfo> parseProcessList(const std::string& text) {
    std::vector<ProcessInfo> processes;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        ProcessInfo process;
        if (parseProcessLine(line, &process)) {
            processes.push_back(process);
        }
    }
    return processes;
}

void pushProcessInfo(lua_State* state, const ProcessInfo& process) {
    lua_newtable(state);
    lua_pushinteger(state, process.pid);
    lua_setfield(state, -2, "pid");
    lua_pushinteger(state, process.ppid);
    lua_setfield(state, -2, "ppid");
    lua_pushstring(state, process.user.c_str());
    lua_setfield(state, -2, "user");
    lua_pushstring(state, process.name.c_str());
    lua_setfield(state, -2, "name");
    lua_pushstring(state, process.args.c_str());
    lua_setfield(state, -2, "args");
}

int pushProcessListResult(lua_State* state,
                          const RootExecResult& result,
                          const char* fallbackError) {
    if (!result.success) {
        std::string error = rootResultError(result, fallbackError);
        lua_pushnil(state);
        lua_pushstring(state, error.c_str());
        return 2;
    }

    std::vector<ProcessInfo> processes = parseProcessList(result.stdoutText);
    lua_createtable(state, static_cast<int>(processes.size()), 0);
    for (size_t i = 0; i < processes.size(); ++i) {
        pushProcessInfo(state, processes[i]);
        lua_rawseti(state, -2, static_cast<lua_Integer>(i + 1));
    }
    return 1;
}

int luaRootProcessPidOf(lua_State* state) {
    const char* processName = luaL_checkstring(state, 1);
    RootExecResult result = AndroidBridge::rootProcessPidOf(processName);
    if (!result.success) {
        std::string error = rootResultError(result, "root process pidOf failed");
        lua_pushnil(state);
        lua_pushstring(state, error.c_str());
        return 2;
    }

    std::vector<int> pids = parsePidList(result.stdoutText);
    lua_createtable(state, static_cast<int>(pids.size()), 0);
    for (size_t i = 0; i < pids.size(); ++i) {
        lua_pushinteger(state, pids[i]);
        lua_rawseti(state, -2, static_cast<lua_Integer>(i + 1));
    }
    return 1;
}

int luaRootProcessList(lua_State* state) {
    RootExecResult result = AndroidBridge::rootProcessList();
    return pushProcessListResult(state, result, "root process list failed");
}

int luaRootProcessInfo(lua_State* state) {
    std::string target;
    if (lua_isinteger(state, 1)) {
        lua_Integer pid = lua_tointeger(state, 1);
        if (pid <= 0) {
            return luaL_error(state, "process pid must be greater than 0");
        }
        target = std::to_string(static_cast<long long>(pid));
    } else {
        target = luaL_checkstring(state, 1);
    }

    RootExecResult result = AndroidBridge::rootProcessInfo(target);
    return pushProcessListResult(state, result, "root process info failed");
}

int luaRootProcessKill(lua_State* state) {
    std::string target;
    if (lua_isinteger(state, 1)) {
        lua_Integer pid = lua_tointeger(state, 1);
        if (pid <= 0) {
            return luaL_error(state, "process pid must be greater than 0");
        }
        target = std::to_string(static_cast<long long>(pid));
    } else {
        target = luaL_checkstring(state, 1);
    }

    lua_Integer signal = luaL_optinteger(state, 2, 15);
    if (signal <= 0) {
        return luaL_error(state, "process signal must be greater than 0");
    }

    RootExecResult result = AndroidBridge::rootProcessKill(target, static_cast<int>(signal));
    if (!result.success) {
        std::string error = rootResultError(result, "root process kill failed");
        lua_pushnil(state);
        lua_pushstring(state, error.c_str());
        return 2;
    }

    lua_pushboolean(state, 1);
    return 1;
}

std::string readAllText(const char* path, std::string* error) {
    FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        *error = "open file failed";
        return "";
    }

    std::string content;
    char buffer[4096];
    while (true) {
        size_t readCount = std::fread(buffer, 1, sizeof(buffer), file);
        if (readCount > 0) {
            content.append(buffer, readCount);
        }

        if (readCount < sizeof(buffer)) {
            if (std::ferror(file)) {
                *error = "read file failed";
                std::fclose(file);
                return "";
            }
            break;
        }
    }

    std::fclose(file);
    return content;
}

bool writeAllText(const char* path, const char* content, size_t contentLength, std::string* error) {
    FILE* file = std::fopen(path, "wb");
    if (file == nullptr) {
        *error = "open file failed";
        return false;
    }

    size_t writeCount = std::fwrite(content, 1, contentLength, file);
    if (writeCount != contentLength) {
        *error = "write file failed";
        std::fclose(file);
        return false;
    }

    std::fclose(file);
    return true;
}

bool fileExists(const char* path) {
    FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        return false;
    }

    std::fclose(file);
    return true;
}

int readImageId(lua_State* state, int index) {
    if (lua_istable(state, index)) {
        lua_getfield(state, index, "id");
        int imageId = static_cast<int>(luaL_checkinteger(state, -1));
        lua_pop(state, 1);
        return imageId;
    }

    return static_cast<int>(luaL_checkinteger(state, index));
}

bool readIntegerFromTable(lua_State* state, int tableIndex, int arrayIndex, int* value) {
    lua_rawgeti(state, tableIndex, arrayIndex);
    int isNumber = 0;
    lua_Integer integerValue = lua_tointegerx(state, -1, &isNumber);
    lua_pop(state, 1);

    if (!isNumber) {
        return false;
    }

    *value = static_cast<int>(integerValue);
    return true;
}

bool readPointCoordinate(
        lua_State* state,
        int pointIndex,
        const char* fieldName,
        int arrayIndex,
        int* value) {
    lua_getfield(state, pointIndex, fieldName);
    int isNumber = 0;
    lua_Integer integerValue = lua_tointegerx(state, -1, &isNumber);
    lua_pop(state, 1);

    if (isNumber) {
        *value = static_cast<int>(integerValue);
        return true;
    }

    return readIntegerFromTable(state, pointIndex, arrayIndex, value);
}

bool readPointTable(lua_State* state, int pointsIndex, lua_Integer pointNumber, PixelPoint* point) {
    lua_rawgeti(state, pointsIndex, pointNumber);
    if (!lua_istable(state, -1)) {
        lua_pop(state, 1);
        return false;
    }

    int pointIndex = lua_gettop(state);
    bool ok = readPointCoordinate(state, pointIndex, "x", 1, &point->x)
            && readPointCoordinate(state, pointIndex, "y", 2, &point->y);
    lua_pop(state, 1);
    return ok;
}

void pushPixelColor(lua_State* state, const PixelColor& color) {
    lua_pushinteger(state, color.rgb);
    lua_pushinteger(state, color.red);
    lua_pushinteger(state, color.green);
    lua_pushinteger(state, color.blue);
    lua_pushinteger(state, color.alpha);
}

void setFunctionField(lua_State* state, int tableIndex, const char* name, lua_CFunction function) {
    lua_pushcfunction(state, function);
    lua_setfield(state, tableIndex, name);
}

int luaFileRead(lua_State* state) {
    const char* path = luaL_checkstring(state, 1);

    std::string error;
    std::string content = readAllText(path, &error);
    if (!error.empty()) {
        lua_pushnil(state);
        lua_pushstring(state, error.c_str());
        return 2;
    }

    lua_pushlstring(state, content.data(), content.size());
    return 1;
}

int luaFileWrite(lua_State* state) {
    const char* path = luaL_checkstring(state, 1);
    size_t contentLength = 0;
    const char* content = luaL_checklstring(state, 2, &contentLength);

    std::string error;
    if (!writeAllText(path, content, contentLength, &error)) {
        lua_pushnil(state);
        lua_pushstring(state, error.c_str());
        return 2;
    }

    lua_pushboolean(state, 1);
    return 1;
}

int luaFileExists(lua_State* state) {
    const char* path = luaL_checkstring(state, 1);
    lua_pushboolean(state, fileExists(path) ? 1 : 0);
    return 1;
}

int luaFileRemove(lua_State* state) {
    const char* path = luaL_checkstring(state, 1);

    if (std::remove(path) != 0) {
        lua_pushnil(state);
        lua_pushstring(state, std::strerror(errno));
        return 2;
    }

    lua_pushboolean(state, 1);
    return 1;
}

int luaFileAppDataPath(lua_State* state) {
    const char* fileName = luaL_checkstring(state, 1);

    // 第一版先写入应用私有 data 目录，避免申请外部存储权限。
    std::string path = EngineConfig::kAppFilesDir;
    path += fileName;
    lua_pushstring(state, path.c_str());
    return 1;
}

int luaAppIsInstalled(lua_State* state) {
    const char* packageName = luaL_checkstring(state, 1);
    lua_pushboolean(state, AndroidBridge::appIsInstalled(packageName) ? 1 : 0);
    return 1;
}

int luaAppOpen(lua_State* state) {
    const char* packageName = luaL_checkstring(state, 1);
    if (!AndroidBridge::appOpen(packageName)) {
        lua_pushnil(state);
        lua_pushstring(state, "open app failed");
        return 2;
    }

    lua_pushboolean(state, 1);
    return 1;
}

int luaAppStop(lua_State* state) {
    const char* packageName = luaL_checkstring(state, 1);
    if (!AndroidBridge::appStop(packageName)) {
        lua_pushnil(state);
        lua_pushstring(state, "stop app failed; root is not available or package name is invalid");
        return 2;
    }

    lua_pushboolean(state, 1);
    return 1;
}

int luaAppClearData(lua_State* state) {
    const char* packageName = luaL_checkstring(state, 1);
    if (!AndroidBridge::appClearData(packageName)) {
        lua_pushnil(state);
        lua_pushstring(state, "clear app data failed; root is not available or package name is invalid");
        return 2;
    }

    lua_pushboolean(state, 1);
    return 1;
}

int luaAppGrantPermission(lua_State* state) {
    const char* packageName = luaL_checkstring(state, 1);
    const char* permissionName = luaL_checkstring(state, 2);
    if (!AndroidBridge::appGrantPermission(packageName, permissionName)) {
        lua_pushnil(state);
        lua_pushstring(state, "grant app permission failed; root is not available, package name is invalid or permission is invalid");
        return 2;
    }

    lua_pushboolean(state, 1);
    return 1;
}

int luaAppRevokePermission(lua_State* state) {
    const char* packageName = luaL_checkstring(state, 1);
    const char* permissionName = luaL_checkstring(state, 2);
    if (!AndroidBridge::appRevokePermission(packageName, permissionName)) {
        lua_pushnil(state);
        lua_pushstring(state, "revoke app permission failed; root is not available, package name is invalid or permission is invalid");
        return 2;
    }

    lua_pushboolean(state, 1);
    return 1;
}

std::string trimText(const std::string& value) {
    size_t start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }

    size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

void pushAppComponent(lua_State* state, const std::string& component) {
    std::string clean = trimText(component);
    size_t separator = clean.find('/');
    std::string packageName = separator == std::string::npos ? clean : clean.substr(0, separator);
    std::string activityName = separator == std::string::npos ? "" : clean.substr(separator + 1);

    lua_newtable(state);
    lua_pushstring(state, clean.c_str());
    lua_setfield(state, -2, "component");
    lua_pushstring(state, packageName.c_str());
    lua_setfield(state, -2, "packageName");
    lua_pushstring(state, activityName.c_str());
    lua_setfield(state, -2, "activityName");
}

int luaAppCurrent(lua_State* state) {
    RootExecResult result = AndroidBridge::appCurrent();
    if (!result.success) {
        std::string error = rootResultError(result, "current app failed");
        lua_pushnil(state);
        lua_pushstring(state, error.c_str());
        return 2;
    }

    std::string component = trimText(result.stdoutText);
    size_t separator = component.find('/');
    if (separator == std::string::npos || separator == 0 || separator >= component.size() - 1) {
        lua_pushnil(state);
        lua_pushstring(state, "current app output is invalid");
        return 2;
    }

    pushAppComponent(state, component);
    return 1;
}

int luaAppInstall(lua_State* state) {
    const char* apkPath = luaL_checkstring(state, 1);
    bool replace = lua_isnoneornil(state, 2) || lua_toboolean(state, 2) != 0;
    if (!AndroidBridge::appInstall(apkPath, replace)) {
        lua_pushnil(state);
        lua_pushstring(state, "install app failed; root is not available or apk path is invalid");
        return 2;
    }

    lua_pushboolean(state, 1);
    return 1;
}

int luaAppUninstall(lua_State* state) {
    const char* packageName = luaL_checkstring(state, 1);
    bool keepData = !lua_isnoneornil(state, 2) && lua_toboolean(state, 2) != 0;
    if (!AndroidBridge::appUninstall(packageName, keepData)) {
        lua_pushnil(state);
        lua_pushstring(state, "uninstall app failed; root is not available or package name is invalid");
        return 2;
    }

    lua_pushboolean(state, 1);
    return 1;
}

int luaAppDisable(lua_State* state) {
    const char* packageName = luaL_checkstring(state, 1);
    if (!AndroidBridge::appDisable(packageName)) {
        lua_pushnil(state);
        lua_pushstring(state, "disable app failed; root is not available or package name is invalid");
        return 2;
    }

    lua_pushboolean(state, 1);
    return 1;
}

int luaAppEnable(lua_State* state) {
    const char* packageName = luaL_checkstring(state, 1);
    if (!AndroidBridge::appEnable(packageName)) {
        lua_pushnil(state);
        lua_pushstring(state, "enable app failed; root is not available or package name is invalid");
        return 2;
    }

    lua_pushboolean(state, 1);
    return 1;
}

int luaAppDisableComponent(lua_State* state) {
    const char* componentName = luaL_checkstring(state, 1);
    if (!AndroidBridge::appDisableComponent(componentName)) {
        lua_pushnil(state);
        lua_pushstring(state, "disable app component failed; root is not available or component name is invalid");
        return 2;
    }

    lua_pushboolean(state, 1);
    return 1;
}

int luaAppEnableComponent(lua_State* state) {
    const char* componentName = luaL_checkstring(state, 1);
    if (!AndroidBridge::appEnableComponent(componentName)) {
        lua_pushnil(state);
        lua_pushstring(state, "enable app component failed; root is not available or component name is invalid");
        return 2;
    }

    lua_pushboolean(state, 1);
    return 1;
}

int luaTouchTap(lua_State* state) {
    lua_Integer x = luaL_checkinteger(state, 1);
    lua_Integer y = luaL_checkinteger(state, 2);

    if (!AndroidBridge::touchTap(static_cast<int>(x), static_cast<int>(y))) {
        lua_pushnil(state);
        lua_pushstring(state, "touch tap failed; root or accessibility service is not available");
        return 2;
    }

    lua_pushboolean(state, 1);
    return 1;
}

int luaTouchSwipe(lua_State* state) {
    lua_Integer x1 = luaL_checkinteger(state, 1);
    lua_Integer y1 = luaL_checkinteger(state, 2);
    lua_Integer x2 = luaL_checkinteger(state, 3);
    lua_Integer y2 = luaL_checkinteger(state, 4);
    lua_Integer durationMs = luaL_optinteger(state, 5, 300);

    if (!AndroidBridge::touchSwipe(
            static_cast<int>(x1),
            static_cast<int>(y1),
            static_cast<int>(x2),
            static_cast<int>(y2),
            static_cast<int>(durationMs))) {
        lua_pushnil(state);
        lua_pushstring(state, "touch swipe failed; root or accessibility service is not available");
        return 2;
    }

    lua_pushboolean(state, 1);
    return 1;
}

int luaInputText(lua_State* state) {
    const char* text = luaL_checkstring(state, 1);
    if (!AndroidBridge::inputText(text)) {
        lua_pushnil(state);
        lua_pushstring(state, "input text failed; root is not available or focused control cannot receive text");
        return 2;
    }

    lua_pushboolean(state, 1);
    return 1;
}

int luaInputPasteText(lua_State* state) {
    const char* text = luaL_checkstring(state, 1);
    if (!AndroidBridge::pasteText(text)) {
        lua_pushnil(state);
        lua_pushstring(state, "paste text failed; root is not available or focused control cannot paste text");
        return 2;
    }

    lua_pushboolean(state, 1);
    return 1;
}

int luaKeyBack(lua_State* state) {
    if (!AndroidBridge::keyBack()) {
        lua_pushnil(state);
        lua_pushstring(state, "key back failed; root or accessibility service is not available");
        return 2;
    }

    lua_pushboolean(state, 1);
    return 1;
}

int luaKeyHome(lua_State* state) {
    if (!AndroidBridge::keyHome()) {
        lua_pushnil(state);
        lua_pushstring(state, "key home failed; root or accessibility service is not available");
        return 2;
    }

    lua_pushboolean(state, 1);
    return 1;
}

int luaKeyPress(lua_State* state) {
    lua_Integer keyCode = luaL_checkinteger(state, 1);
    if (!AndroidBridge::keyPress(static_cast<int>(keyCode))) {
        lua_pushnil(state);
        lua_pushstring(state, "key press failed; root or accessibility service is not available");
        return 2;
    }

    lua_pushboolean(state, 1);
    return 1;
}

int luaKeyIsAccessibilityEnabled(lua_State* state) {
    lua_pushboolean(state, AndroidBridge::isAccessibilityEnabled() ? 1 : 0);
    return 1;
}

int luaScreenCapture(lua_State* state) {
    if (!AndroidBridge::hasScreenCapturePermission()) {
        lua_pushnil(state);
        lua_pushstring(state, "screen capture permission is not granted");
        return 2;
    }

    ScreenCaptureResult capture = AndroidBridge::captureScreen();
    if (!capture.success) {
        lua_pushnil(state);
        lua_pushstring(state, capture.error.c_str());
        return 2;
    }

    ImageFrame frame;
    frame.width = capture.width;
    frame.height = capture.height;
    frame.rowStride = capture.rowStride;
    frame.pixelStride = capture.pixelStride;
    frame.format = capture.format;
    frame.pixels = std::move(capture.pixels);

    ImageMetadata metadata = storeImageFrame(std::move(frame));

    lua_newtable(state);
    lua_pushinteger(state, metadata.id);
    lua_setfield(state, -2, "id");
    lua_pushstring(state, "image");
    lua_setfield(state, -2, "type");
    lua_pushinteger(state, metadata.width);
    lua_setfield(state, -2, "width");
    lua_pushinteger(state, metadata.height);
    lua_setfield(state, -2, "height");
    lua_pushinteger(state, metadata.rowStride);
    lua_setfield(state, -2, "rowStride");
    lua_pushinteger(state, metadata.pixelStride);
    lua_setfield(state, -2, "pixelStride");
    lua_pushinteger(state, static_cast<lua_Integer>(metadata.byteLength));
    lua_setfield(state, -2, "byteLength");
    lua_pushstring(state, metadata.format.c_str());
    lua_setfield(state, -2, "format");
    return 1;
}

int luaImageRelease(lua_State* state) {
    int imageId = readImageId(state, 1);
    if (!releaseImageFrame(imageId)) {
        lua_pushnil(state);
        lua_pushstring(state, "image handle is not found");
        return 2;
    }

    lua_pushboolean(state, 1);
    return 1;
}

int luaImageGetPixel(lua_State* state) {
    int imageId = readImageId(state, 1);
    int x = static_cast<int>(luaL_checkinteger(state, 2));
    int y = static_cast<int>(luaL_checkinteger(state, 3));

    PixelColor color;
    std::string error;
    if (!readImagePixel(imageId, x, y, &color, &error)) {
        lua_pushnil(state);
        lua_pushstring(state, error.c_str());
        return 2;
    }

    pushPixelColor(state, color);
    return 5;
}

int luaImageGetPixels(lua_State* state) {
    int imageId = readImageId(state, 1);
    int pointsIndex = lua_absindex(state, 2);
    luaL_checktype(state, pointsIndex, LUA_TTABLE);

    lua_Integer rawLength = static_cast<lua_Integer>(lua_rawlen(state, pointsIndex));
    std::vector<PixelPoint> points;

    lua_rawgeti(state, pointsIndex, 1);
    bool useFlatPointList = lua_isnumber(state, -1);
    lua_pop(state, 1);

    if (useFlatPointList) {
        if (rawLength % 2 != 0) {
            lua_pushnil(state);
            lua_pushstring(state, "flat point list length must be even");
            return 2;
        }

        points.reserve(static_cast<size_t>(rawLength / 2));
        for (lua_Integer i = 1; i <= rawLength; i += 2) {
            PixelPoint point;
            if (!readIntegerFromTable(state, pointsIndex, static_cast<int>(i), &point.x)
                    || !readIntegerFromTable(state, pointsIndex, static_cast<int>(i + 1), &point.y)) {
                lua_pushnil(state);
                lua_pushstring(state, "flat point list must contain number pairs");
                return 2;
            }
            points.push_back(point);
        }
    } else {
        points.reserve(static_cast<size_t>(rawLength));
        for (lua_Integer i = 1; i <= rawLength; ++i) {
            PixelPoint point;
            if (!readPointTable(state, pointsIndex, i, &point)) {
                lua_pushnil(state);
                lua_pushfstring(state, "point #%d must contain x and y", static_cast<int>(i));
                return 2;
            }
            points.push_back(point);
        }
    }

    std::vector<int> colors;
    std::string error;
    if (!readImagePixels(imageId, points, &colors, &error)) {
        lua_pushnil(state);
        lua_pushstring(state, error.c_str());
        return 2;
    }

    lua_createtable(state, static_cast<int>(colors.size()), 0);
    for (size_t i = 0; i < colors.size(); ++i) {
        lua_pushinteger(state, colors[i]);
        lua_rawseti(state, -2, static_cast<lua_Integer>(i + 1));
    }

    return 1;
}

} // namespace

void registerHostApi(lua_State* state) {
    lua_newtable(state);
    int hostTableIndex = lua_gettop(state);

    setFunctionField(state, hostTableIndex, "print", luaPrint);
    setFunctionField(state, hostTableIndex, "sleep", luaSleep);

    lua_newtable(state);
    int logTableIndex = lua_gettop(state);
    setFunctionField(state, logTableIndex, "print", luaLogPrint);
    lua_setfield(state, hostTableIndex, "log");

    lua_newtable(state);
    int deviceTableIndex = lua_gettop(state);
    setFunctionField(state, deviceTableIndex, "info", luaDeviceInfo);
    setFunctionField(state, deviceTableIndex, "isRootAvailable", luaDeviceIsRootAvailable);
    setFunctionField(state, deviceTableIndex, "setRootModeEnabled", luaDeviceSetRootModeEnabled);
    setFunctionField(state, deviceTableIndex, "screenState", luaDeviceScreenState);
    setFunctionField(state, deviceTableIndex, "wake", luaDeviceWake);
    setFunctionField(state, deviceTableIndex, "sleep", luaDeviceSleep);
    setFunctionField(state, deviceTableIndex, "battery", luaDeviceBattery);
    setFunctionField(state, deviceTableIndex, "rotation", luaDeviceRotation);
    setFunctionField(state, deviceTableIndex, "setRotation", luaDeviceSetRotation);
    lua_setfield(state, hostTableIndex, "device");

    lua_newtable(state);
    int rootTableIndex = lua_gettop(state);
    setFunctionField(state, rootTableIndex, "exec", luaRootExec);
    setFunctionField(state, rootTableIndex, "isAvailable", luaDeviceIsRootAvailable);
    setFunctionField(state, rootTableIndex, "status", luaRootStatus);

    lua_newtable(state);
    int rootFileTableIndex = lua_gettop(state);
    setFunctionField(state, rootFileTableIndex, "exists", luaRootFileExists);
    setFunctionField(state, rootFileTableIndex, "readText", luaRootFileReadText);
    setFunctionField(state, rootFileTableIndex, "writeText", luaRootFileWriteText);
    setFunctionField(state, rootFileTableIndex, "stat", luaRootFileStat);
    setFunctionField(state, rootFileTableIndex, "list", luaRootFileList);
    setFunctionField(state, rootFileTableIndex, "remove", luaRootFileRemove);
    setFunctionField(state, rootFileTableIndex, "mkdir", luaRootFileMkdir);
    setFunctionField(state, rootFileTableIndex, "chmod", luaRootFileChmod);
    setFunctionField(state, rootFileTableIndex, "chown", luaRootFileChown);
    lua_setfield(state, rootTableIndex, "file");

    lua_newtable(state);
    int rootProcessTableIndex = lua_gettop(state);
    setFunctionField(state, rootProcessTableIndex, "pidOf", luaRootProcessPidOf);
    setFunctionField(state, rootProcessTableIndex, "list", luaRootProcessList);
    setFunctionField(state, rootProcessTableIndex, "info", luaRootProcessInfo);
    setFunctionField(state, rootProcessTableIndex, "kill", luaRootProcessKill);
    lua_setfield(state, rootTableIndex, "process");

    lua_setfield(state, hostTableIndex, "root");

    lua_newtable(state);
    int fileTableIndex = lua_gettop(state);
    setFunctionField(state, fileTableIndex, "read", luaFileRead);
    setFunctionField(state, fileTableIndex, "write", luaFileWrite);
    setFunctionField(state, fileTableIndex, "exists", luaFileExists);
    setFunctionField(state, fileTableIndex, "remove", luaFileRemove);
    setFunctionField(state, fileTableIndex, "appDataPath", luaFileAppDataPath);
    lua_setfield(state, hostTableIndex, "file");

    lua_newtable(state);
    int appTableIndex = lua_gettop(state);
    setFunctionField(state, appTableIndex, "isInstalled", luaAppIsInstalled);
    setFunctionField(state, appTableIndex, "open", luaAppOpen);
    setFunctionField(state, appTableIndex, "start", luaAppOpen);
    setFunctionField(state, appTableIndex, "stop", luaAppStop);
    setFunctionField(state, appTableIndex, "clearData", luaAppClearData);
    setFunctionField(state, appTableIndex, "grant", luaAppGrantPermission);
    setFunctionField(state, appTableIndex, "revoke", luaAppRevokePermission);
    setFunctionField(state, appTableIndex, "current", luaAppCurrent);
    setFunctionField(state, appTableIndex, "install", luaAppInstall);
    setFunctionField(state, appTableIndex, "uninstall", luaAppUninstall);
    setFunctionField(state, appTableIndex, "disable", luaAppDisable);
    setFunctionField(state, appTableIndex, "enable", luaAppEnable);
    setFunctionField(state, appTableIndex, "disableComponent", luaAppDisableComponent);
    setFunctionField(state, appTableIndex, "enableComponent", luaAppEnableComponent);
    lua_setfield(state, hostTableIndex, "app");

    lua_newtable(state);
    int touchTableIndex = lua_gettop(state);
    setFunctionField(state, touchTableIndex, "tap", luaTouchTap);
    setFunctionField(state, touchTableIndex, "swipe", luaTouchSwipe);
    lua_setfield(state, hostTableIndex, "touch");

    lua_newtable(state);
    int inputTableIndex = lua_gettop(state);
    setFunctionField(state, inputTableIndex, "text", luaInputText);
    setFunctionField(state, inputTableIndex, "pasteText", luaInputPasteText);
    lua_setfield(state, hostTableIndex, "input");

    lua_newtable(state);
    int keyTableIndex = lua_gettop(state);
    setFunctionField(state, keyTableIndex, "isAccessibilityEnabled", luaKeyIsAccessibilityEnabled);
    setFunctionField(state, keyTableIndex, "press", luaKeyPress);
    setFunctionField(state, keyTableIndex, "back", luaKeyBack);
    setFunctionField(state, keyTableIndex, "home", luaKeyHome);
    lua_setfield(state, hostTableIndex, "key");

    lua_newtable(state);
    int screenTableIndex = lua_gettop(state);
    setFunctionField(state, screenTableIndex, "capture", luaScreenCapture);
    lua_setfield(state, hostTableIndex, "screen");

    lua_newtable(state);
    int imageTableIndex = lua_gettop(state);
    setFunctionField(state, imageTableIndex, "release", luaImageRelease);
    setFunctionField(state, imageTableIndex, "getPixel", luaImageGetPixel);
    setFunctionField(state, imageTableIndex, "getPixels", luaImageGetPixels);
    lua_setfield(state, hostTableIndex, "image");

    lua_setglobal(state, "_host");
}
