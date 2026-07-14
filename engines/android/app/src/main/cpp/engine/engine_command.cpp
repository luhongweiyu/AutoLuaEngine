/**
 * 文件用途：实现引擎 JSON 命令分发，只保留当前真实可用的控制协议。
 */
#include "engine_command.h"

#include "engine.h"
#include "engine_config.h"
#include "json_value.h"
#include "../platform/android_bridge.h"
#include "../core/api/ui_api.h"
#include "../runtime/common/log_buffer.h"
#include "../runtime/lua/alpkg_package.h"

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
        throw CommandError(-32602, name + " 参数不能为空");
    }
    return value;
}

std::string requireString(const JsonValue& params, const std::string& name) {
    const JsonValue* value = requireField(params, name);
    if (!value->isString()) {
        throw CommandError(-32602, name + " 参数必须是字符串");
    }

    std::string text = value->stringValue();
    if (trim(text).empty()) {
        throw CommandError(-32602, name + " 参数不能为空");
    }
    return text;
}

bool requireBool(const JsonValue& params, const std::string& name) {
    const JsonValue* value = requireField(params, name);
    if (!value->isBool()) {
        throw CommandError(-32602, name + " 参数必须是布尔值");
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
    if (message == "已有脚本正在运行") {
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

/**
 * 打开并运行 Android 本地已存在的 ALPKG 包。
 *
 * Java 只传包的真实路径；ZIP 解析、manifest 校验、字节码认证和 Lua 加载都在
 * libengine.so 内完成，保证以后 VS Code、Qt、JS 或 Go 都能复用同一条运行路线。
 */
std::string runPackageScript(
        Engine& engine,
        const JsonValue& params,
        const std::string& luaRuntimeBootstrap) {
    std::string packagePath = requireString(params, "packagePath");
    std::string packageError;
    std::shared_ptr<AlpkgPackage> package = AlpkgPackage::open(packagePath, &packageError);
    if (package == nullptr) {
        throw CommandError(-32602, packageError.empty() ? "脚本包打开失败" : packageError);
    }

    std::string message = engine.runLuaPackage(package, luaRuntimeBootstrap.c_str());
    if (message == "已有脚本正在运行") {
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

/**
 * 接收 App 主进程的脚本 UI 事件。
 *
 * Dialog Overlay Service、HUD Service 和 WebView Activity 都通过本机 EngineHttpServer 调用
 * ui.event。事件只写入 native UI 会话队列，绝不在 Android 主线程直接进入 Lua VM。
 */
std::string deliverUiEventCommand(const JsonValue& params) {
    const JsonValue* sessionIdValue = requireField(params, "sessionId");
    if (!sessionIdValue->isNumber()) {
        throw CommandError(-32602, "sessionId 参数必须是数字");
    }

    long long sessionId = sessionIdValue->longValue();
    if (sessionId <= 0) {
        throw CommandError(-32602, "sessionId 参数必须大于 0");
    }

    std::string eventType = params.stringOr("event", "event");
    if (eventType.empty()) {
        throw CommandError(-32602, "event 参数不能为空");
    }

    const JsonValue* data = params.get("data");
    bool accepted = xiaoyv::api::deliverUiEvent(
            sessionId,
            eventType,
            data == nullptr ? "null" : jsonValueToString(*data)
    );
    return "{\"accepted\":" + boolText(accepted) + "}";
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

        return makeDeviceInfoJson();
    }

    if (method == "script.run") {
        return runScript(engine, params, luaRuntimeBootstrap);
    }

    if (method == "script.runPackage") {
        return runPackageScript(engine, params, luaRuntimeBootstrap);
    }

    if (method == "script.stop") {
        bool accepted = engine.requestStop();
        JsonValue status;
        std::string parseError;
        parseJsonText(engine.statusJson(0), &status, &parseError);
        return "{\"accepted\":"
                + boolText(accepted)
                + ",\"status\":"
                + quoteJsonString(status.stringOr("status", "unknown"))
                + "}";
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

    if (method == "ui.event") {
        return deliverUiEventCommand(params);
    }

    if (method == "ui.closeAll") {
        xiaoyv::api::closeAllUiSurfaces();
        return "{\"closed\":true}";
    }

    throw CommandError(-32601, "未找到命令：" + method);
}

} // namespace

std::string handleEngineCommand(Engine& engine,
                                const std::string& method,
                                const std::string& paramsJson,
                                const std::string& luaRuntimeBootstrap) {
    try {
        if (trim(method).empty()) {
            throw CommandError(-32600, "命令名称不能为空");
        }

        JsonValue params;
        std::string parseError;
        std::string paramsText = trim(paramsJson).empty() ? "{}" : paramsJson;
        if (!parseJsonText(paramsText, &params, &parseError)) {
            throw CommandError(-32602, parseError.empty() ? "参数 JSON 无效" : parseError);
        }
        if (!params.isObject()) {
            throw CommandError(-32602, "参数必须是对象");
        }

        return okRaw(commandResult(engine, method, params, luaRuntimeBootstrap));
    } catch (const CommandError& error) {
        return errorJson(error.code(), error.what());
    } catch (const std::exception& error) {
        return errorJson(-32000, error.what());
    } catch (...) {
        return errorJson(-32000, "原生命令执行失败");
    }
}
