/**
 * 文件用途：实现引擎 JSON 命令分发，只保留当前真实可用的控制协议。
 */
#include "engine_command.h"

#include "engine.h"
#include "engine_config.h"
#include "json_value.h"
#include "../platform/android_bridge.h"
#include "../runtime/common/log_buffer.h"

#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
#include "lua.h"
}

namespace {

class CommandError : public std::runtime_error {
public:
    CommandError(int code, const std::string& message)
            : std::runtime_error(message),
              code_(code) {
    }

    int code() const {
        return code_;
    }

private:
    int code_;
};

std::string trim(const std::string& value) {
    size_t start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }

    size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

std::string okRaw(const std::string& resultJson) {
    return "{\"ok\":true,\"result\":" + resultJson + "}";
}

std::string errorJson(int code, const std::string& message) {
    return "{\"ok\":false,\"code\":"
            + std::to_string(code)
            + ",\"error\":"
            + quoteJsonString(message)
            + "}";
}

std::string boolText(bool value) {
    return value ? "true" : "false";
}

const JsonValue* requireField(const JsonValue& params, const std::string& name) {
    const JsonValue* value = params.get(name);
    if (value == nullptr) {
        throw CommandError(-32602, name + " is required");
    }
    return value;
}

std::string requireString(const JsonValue& params, const std::string& name) {
    const JsonValue* value = requireField(params, name);
    if (!value->isString()) {
        throw CommandError(-32602, name + " must be a string");
    }

    std::string text = value->stringValue();
    if (trim(text).empty()) {
        throw CommandError(-32602, name + " is required");
    }
    return text;
}

bool requireBool(const JsonValue& params, const std::string& name) {
    const JsonValue* value = requireField(params, name);
    if (!value->isBool()) {
        throw CommandError(-32602, name + " must be a boolean");
    }
    return value->boolValue();
}

std::string makeRootStatusJson(const RootStatusResult& status) {
    std::ostringstream output;
    output << "{";
    output << "\"available\":" << boolText(status.available) << ",";
    output << "\"commandMode\":" << quoteJsonString(status.commandMode) << ",";
    output << "\"suPath\":" << quoteJsonString(status.suPath) << ",";
    output << "\"cached\":" << boolText(status.cached) << ",";
    output << "\"cacheExpireAt\":" << status.cacheExpireAt << ",";
    output << "\"error\":" << quoteJsonString(status.error) << ",";
    output << "\"attempts\":[";
    for (size_t i = 0; i < status.attempts.size(); ++i) {
        const RootProbeAttempt& attempt = status.attempts[i];
        if (i > 0) {
            output << ",";
        }
        output << "{";
        output << "\"commandMode\":" << quoteJsonString(attempt.commandMode) << ",";
        output << "\"suPath\":" << quoteJsonString(attempt.suPath) << ",";
        output << "\"exitCode\":" << attempt.exitCode << ",";
        output << "\"stdout\":" << quoteJsonString(attempt.stdoutText) << ",";
        output << "\"stderr\":" << quoteJsonString(attempt.stderrText) << ",";
        output << "\"timedOut\":" << boolText(attempt.timedOut) << ",";
        output << "\"error\":" << quoteJsonString(attempt.error);
        output << "}";
    }
    output << "]}";
    return output.str();
}

std::string automationMode(bool rootModeEnabled, bool rootAvailable, bool accessibilityEnabled) {
    if (rootModeEnabled && rootAvailable) {
        return "root";
    }
    if (accessibilityEnabled) {
        return "accessibility";
    }
    return "none";
}

std::string makeDeviceInfoJson() {
    bool rootModeEnabled = AndroidBridge::isRootModeEnabled();
    RootStatusResult rootStatus = AndroidBridge::rootStatus();
    bool accessibilityEnabled = AndroidBridge::isAccessibilityEnabled();

    std::ostringstream output;
    output << "{";
    output << "\"platform\":\"android\",";
    output << "\"engineVersion\":" << quoteJsonString(EngineConfig::kEngineVersion) << ",";
    output << "\"luaVersion\":" << quoteJsonString(LUA_VERSION) << ",";
    output << "\"apiLevel\":" << AndroidBridge::apiLevel() << ",";
    output << "\"packageName\":" << quoteJsonString(AndroidBridge::packageName()) << ",";
    output << "\"rootModeEnabled\":" << boolText(rootModeEnabled) << ",";
    output << "\"rootAvailable\":" << boolText(rootStatus.available) << ",";
    output << "\"rootRuntimeReady\":" << boolText(AndroidBridge::isRootRuntimeReady()) << ",";
    output << "\"rootStatus\":" << makeRootStatusJson(rootStatus) << ",";
    output << "\"accessibilityEnabled\":" << boolText(accessibilityEnabled) << ",";
    output << "\"automationMode\":"
           << quoteJsonString(automationMode(rootModeEnabled, rootStatus.available, accessibilityEnabled))
           << ",";
    output << "\"httpHost\":\"127.0.0.1\",";
    output << "\"httpPort\":" << AndroidBridge::httpPort();
    output << "}";
    return output.str();
}

std::string runScript(Engine& engine,
                      const JsonValue& params,
                      const std::string& luaRuntimeBootstrap) {
    std::string language = params.stringOr("language", "lua");
    if (language != "lua") {
        throw CommandError(-32602, "当前只支持 Lua 脚本");
    }

    std::string code = requireString(params, "code");
    std::string message = engine.runLuaText((luaRuntimeBootstrap + "\n" + code).c_str());
    if (message == "Engine is already running") {
        throw CommandError(-32000, "已有脚本正在运行");
    }

    JsonValue status;
    std::string parseError;
    if (!parseJsonText(engine.statusJson(0), &status, &parseError) || !status.isObject()) {
        throw CommandError(-32000, "脚本状态解析失败");
    }

    std::ostringstream output;
    output << "{";
    output << "\"taskId\":" << status.intOr("taskId", 0) << ",";
    output << "\"message\":" << quoteJsonString(message) << ",";
    output << "\"status\":" << quoteJsonString(status.stringOr("status", "unknown"));
    output << "}";
    return output.str();
}

std::string drainLogs(const JsonValue& params) {
    int afterId = params.intOr("afterId", 0);
    std::vector<LogEntry> entries = drainLogEntries(afterId);
    int lastId = afterId;

    std::ostringstream output;
    output << "{\"entries\":[";
    for (size_t i = 0; i < entries.size(); ++i) {
        const LogEntry& entry = entries[i];
        if (i > 0) {
            output << ",";
        }
        output << "{";
        output << "\"id\":" << entry.id << ",";
        output << "\"level\":" << quoteJsonString(entry.level) << ",";
        output << "\"message\":" << quoteJsonString(entry.message);
        output << "}";
        lastId = entry.id;
    }
    output << "],\"lastId\":" << lastId << "}";
    return output.str();
}

std::string commandResult(Engine& engine,
                          const std::string& method,
                          const JsonValue& params,
                          const std::string& luaRuntimeBootstrap) {
    if (method == "device.info") {
        return makeDeviceInfoJson();
    }

    if (method == "device.setRootModeEnabled") {
        bool enabled = requireBool(params, "enabled");
        if (!AndroidBridge::setRootModeEnabled(enabled)) {
            throw CommandError(-32000, "切换 Root 模式失败");
        }

        if (enabled) {
            AndroidBridge::prepareRootRuntime();
            if (AndroidBridge::isRootRuntimeReady()) {
                AndroidBridge::prepareRootHelper();
            }
        }
        return makeDeviceInfoJson();
    }

    if (method == "script.run") {
        return runScript(engine, params, luaRuntimeBootstrap);
    }

    if (method == "script.stop") {
        engine.requestStop();
        return "{\"accepted\":true}";
    }

    if (method == "script.pause") {
        bool accepted = engine.requestPause();
        JsonValue status;
        std::string parseError;
        parseJsonText(engine.statusJson(0), &status, &parseError);
        return "{\"accepted\":"
                + boolText(accepted)
                + ",\"status\":"
                + quoteJsonString(status.stringOr("status", "unknown"))
                + "}";
    }

    if (method == "script.resume") {
        bool accepted = engine.requestResume();
        JsonValue status;
        std::string parseError;
        parseJsonText(engine.statusJson(0), &status, &parseError);
        return "{\"accepted\":"
                + boolText(accepted)
                + ",\"status\":"
                + quoteJsonString(status.stringOr("status", "unknown"))
                + "}";
    }

    if (method == "script.status") {
        return engine.statusJson(params.intOr("taskId", 0));
    }

    if (method == "log.drain") {
        return drainLogs(params);
    }

    throw CommandError(-32601, "method is not found: " + method);
}

} // namespace

std::string handleEngineCommand(Engine& engine,
                                const std::string& method,
                                const std::string& paramsJson,
                                const std::string& luaRuntimeBootstrap) {
    try {
        if (trim(method).empty()) {
            throw CommandError(-32600, "method is required");
        }

        JsonValue params;
        std::string parseError;
        std::string paramsText = trim(paramsJson).empty() ? "{}" : paramsJson;
        if (!parseJsonText(paramsText, &params, &parseError)) {
            throw CommandError(-32602, parseError.empty() ? "params json is invalid" : parseError);
        }
        if (!params.isObject()) {
            throw CommandError(-32602, "params must be an object");
        }

        return okRaw(commandResult(engine, method, params, luaRuntimeBootstrap));
    } catch (const CommandError& error) {
        return errorJson(error.code(), error.what());
    } catch (const std::exception& error) {
        return errorJson(-32000, error.what());
    } catch (...) {
        return errorJson(-32000, "native command failed");
    }
}
