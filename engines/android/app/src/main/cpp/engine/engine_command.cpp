/**
 * 文件用途：实现 JSON-RPC 命令分发，把 HTTP/Java 请求映射到 native 系统 API。
 */
#include "engine_command.h"

#include "engine.h"
#include "engine_config.h"
#include "json_value.h"
#include "../core/system_api.h"
#include "../runtime/common/image_store.h"
#include "../runtime/common/log_buffer.h"

#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include "lua.h"
}

using autolua::core::SystemApi;

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

std::string rootResultError(const RootExecResult& result, const char* defaultError) {
    if (!result.error.empty()) {
        return result.error;
    }
    if (!result.stderrText.empty()) {
        return result.stderrText;
    }
    return defaultError;
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

std::string requireStringAllowEmpty(const JsonValue& params, const std::string& name) {
    const JsonValue* value = requireField(params, name);
    if (!value->isString()) {
        throw CommandError(-32602, name + " must be a string");
    }
    return value->stringValue();
}

int requireInt(const JsonValue& params, const std::string& name) {
    const JsonValue* value = requireField(params, name);
    if (!value->isNumber()) {
        throw CommandError(-32602, name + " must be an integer");
    }
    return value->intValue();
}

int requirePositiveInt(const JsonValue& params, const std::string& name) {
    int value = requireInt(params, name);
    if (value <= 0) {
        throw CommandError(-32602, name + " must be greater than 0");
    }
    return value;
}

bool requireBool(const JsonValue& params, const std::string& name) {
    const JsonValue* value = requireField(params, name);
    if (!value->isBool()) {
        throw CommandError(-32602, name + " must be a boolean");
    }
    return value->boolValue();
}

std::string requirePackageName(const JsonValue& params) {
    return requireString(params, "packageName");
}

std::string requirePermissionName(const JsonValue& params) {
    return requireString(params, "permission");
}

std::string requirePath(const JsonValue& params) {
    return requireString(params, "path");
}

std::string requireApkPath(const JsonValue& params) {
    const JsonValue* path = params.get("path");
    if (path != nullptr && path->isString() && !trim(path->stringValue()).empty()) {
        return path->stringValue();
    }

    const JsonValue* apkPath = params.get("apkPath");
    if (apkPath != nullptr && apkPath->isString() && !trim(apkPath->stringValue()).empty()) {
        return apkPath->stringValue();
    }

    throw CommandError(-32602, "path is required");
}

std::string requireComponentName(const JsonValue& params) {
    const JsonValue* component = params.get("component");
    if (component != nullptr && component->isString() && !trim(component->stringValue()).empty()) {
        return component->stringValue();
    }

    const JsonValue* componentName = params.get("componentName");
    if (componentName != nullptr
            && componentName->isString()
            && !trim(componentName->stringValue()).empty()) {
        return componentName->stringValue();
    }

    throw CommandError(-32602, "component is required");
}

std::string requireSettingsNamespace(const JsonValue& params) {
    return requireString(params, "namespace");
}

std::string requireKey(const JsonValue& params) {
    return requireString(params, "key");
}

std::string requireValue(const JsonValue& params) {
    return requireStringAllowEmpty(params, "value");
}

std::string requireProcessName(const JsonValue& params) {
    return requireString(params, "name");
}

std::string requireProcessTarget(const JsonValue& params) {
    const JsonValue* target = params.get("target");
    if (target != nullptr && target->isString() && !trim(target->stringValue()).empty()) {
        return target->stringValue();
    }

    const JsonValue* pid = params.get("pid");
    if (pid != nullptr && pid->isNumber() && pid->intValue() > 0) {
        return std::to_string(pid->intValue());
    }

    const JsonValue* name = params.get("name");
    if (name != nullptr && name->isString() && !trim(name->stringValue()).empty()) {
        return name->stringValue();
    }

    throw CommandError(-32602, "target, pid or name is required");
}

bool isIntegerText(const std::string& value) {
    if (value.empty()) {
        return false;
    }

    size_t index = value[0] == '-' ? 1 : 0;
    if (index >= value.size()) {
        return false;
    }
    for (; index < value.size(); ++index) {
        if (value[index] < '0' || value[index] > '9') {
            return false;
        }
    }
    return true;
}

bool isNumberText(const std::string& value) {
    bool dotSeen = false;
    bool digitSeen = false;
    size_t index = value.empty() || value[0] != '-' ? 0 : 1;
    for (; index < value.size(); ++index) {
        char ch = value[index];
        if (ch >= '0' && ch <= '9') {
            digitSeen = true;
            continue;
        }
        if (ch == '.' && !dotSeen) {
            dotSeen = true;
            continue;
        }
        return false;
    }
    return dotSeen && digitSeen;
}

std::string typedJsonValue(const std::string& value) {
    if (value == "true" || value == "false") {
        return value;
    }
    if (isIntegerText(value)) {
        return value;
    }
    if (isNumberText(value)) {
        return value;
    }
    return quoteJsonString(value);
}

std::string parseKeyValueObject(const std::string& text) {
    std::ostringstream output;
    output << "{";
    bool first = true;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        size_t separator = line.find('=');
        if (separator == std::string::npos || separator == 0) {
            continue;
        }

        std::string key = trim(line.substr(0, separator));
        std::string value = trim(line.substr(separator + 1));
        if (key.empty()) {
            continue;
        }

        if (!first) {
            output << ",";
        }
        first = false;
        output << quoteJsonString(key) << ":" << typedJsonValue(value);
    }
    output << "}";
    return output.str();
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

std::string makeRootExecResultJson(const RootExecResult& result) {
    std::ostringstream output;
    output << "{";
    output << "\"ok\":" << boolText(result.success) << ",";
    output << "\"exitCode\":" << result.exitCode << ",";
    output << "\"stdout\":" << quoteJsonString(result.stdoutText) << ",";
    output << "\"stderr\":" << quoteJsonString(result.stderrText) << ",";
    output << "\"timedOut\":" << boolText(result.timedOut) << ",";
    output << "\"error\":" << quoteJsonString(result.error);
    output << "}";
    return output.str();
}

std::string automationMode(bool rootModeEnabled, bool rootAvailable, bool accessibilityEnabled) {
    if (rootModeEnabled && rootAvailable) {
        return "root-first";
    }
    if (accessibilityEnabled) {
        return "accessibility";
    }
    return "none";
}

std::string makeDeviceInfoJson() {
    bool rootModeEnabled = SystemApi::isRootModeEnabled();
    RootStatusResult rootStatus = SystemApi::rootStatus();
    bool rootAvailable = rootStatus.available;
    bool accessibilityEnabled = SystemApi::isAccessibilityEnabled();

    std::ostringstream output;
    output << "{";
    output << "\"platform\":\"android\",";
    output << "\"engineVersion\":" << quoteJsonString(EngineConfig::kEngineVersion) << ",";
    output << "\"luaVersion\":" << quoteJsonString(LUA_VERSION) << ",";
    output << "\"apiLevel\":" << SystemApi::apiLevel() << ",";
    output << "\"packageName\":" << quoteJsonString(SystemApi::packageName()) << ",";
    output << "\"rootModeEnabled\":" << boolText(rootModeEnabled) << ",";
    output << "\"rootAvailable\":" << boolText(rootAvailable) << ",";
    output << "\"rootStatus\":" << makeRootStatusJson(rootStatus) << ",";
    output << "\"accessibilityEnabled\":" << boolText(accessibilityEnabled) << ",";
    output << "\"automationMode\":"
           << quoteJsonString(automationMode(rootModeEnabled, rootAvailable, accessibilityEnabled))
           << ",";
    output << "\"httpHost\":\"127.0.0.1\",";
    output << "\"httpPort\":" << SystemApi::httpPort();
    output << "}";
    return output.str();
}

std::string rootFileBaseName(const std::string& path) {
    size_t index = path.find_last_of('/');
    if (index == std::string::npos) {
        return path;
    }
    if (index + 1 >= path.size()) {
        return "";
    }
    return path.substr(index + 1);
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

bool parseLongLong(const std::string& text, long long* value) {
    char* end = nullptr;
    long long parsed = std::strtoll(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0') {
        return false;
    }
    *value = parsed;
    return true;
}

std::string parseRootFileInfoOrEmpty(const std::string& line) {
    std::vector<std::string> parts = splitStatLine(trim(line));
    if (parts.size() != 9) {
        return "";
    }

    long long size = 0;
    long long uid = 0;
    long long gid = 0;
    long long modifiedAt = 0;
    if (!parseLongLong(parts[1], &size)
            || !parseLongLong(parts[5], &uid)
            || !parseLongLong(parts[6], &gid)
            || !parseLongLong(parts[7], &modifiedAt)) {
        return "";
    }

    std::ostringstream output;
    output << "{";
    output << "\"type\":" << quoteJsonString(parts[0]) << ",";
    output << "\"size\":" << size << ",";
    output << "\"mode\":" << quoteJsonString(parts[2]) << ",";
    output << "\"user\":" << quoteJsonString(parts[3]) << ",";
    output << "\"group\":" << quoteJsonString(parts[4]) << ",";
    output << "\"uid\":" << uid << ",";
    output << "\"gid\":" << gid << ",";
    output << "\"modifiedAt\":" << modifiedAt << ",";
    output << "\"path\":" << quoteJsonString(parts[8]) << ",";
    output << "\"name\":" << quoteJsonString(rootFileBaseName(parts[8]));
    output << "}";
    return output.str();
}

std::string parseRootFileInfo(const std::string& text) {
    std::string file = parseRootFileInfoOrEmpty(text);
    if (file.empty()) {
        throw CommandError(-32000, "root file stat output is invalid");
    }
    return file;
}

std::string parseRootFileInfoArray(const std::string& text) {
    std::ostringstream output;
    output << "[";
    bool first = true;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        std::string file = parseRootFileInfoOrEmpty(line);
        if (file.empty()) {
            continue;
        }
        if (!first) {
            output << ",";
        }
        first = false;
        output << file;
    }
    output << "]";
    return output.str();
}

std::string parsePidArray(const std::string& text) {
    std::ostringstream output;
    output << "[";
    bool first = true;
    std::istringstream stream(text);
    std::string part;
    while (stream >> part) {
        long long pid = 0;
        if (!parseLongLong(part, &pid)) {
            continue;
        }
        if (!first) {
            output << ",";
        }
        first = false;
        output << pid;
    }
    output << "]";
    return output.str();
}

std::string parseProcessLineOrEmpty(const std::string& line) {
    std::string clean = trim(line);
    if (clean.empty() || clean.rfind("PID ", 0) == 0 || clean.find(" PID ") != std::string::npos) {
        return "";
    }

    std::istringstream stream(clean);
    std::string pidText;
    std::string ppidText;
    std::string user;
    std::string name;
    if (!(stream >> pidText >> ppidText >> user >> name)) {
        return "";
    }

    long long pid = 0;
    long long ppid = 0;
    if (!parseLongLong(pidText, &pid) || !parseLongLong(ppidText, &ppid)) {
        return "";
    }

    std::string args;
    std::getline(stream, args);
    args = trim(args);

    std::ostringstream output;
    output << "{";
    output << "\"pid\":" << pid << ",";
    output << "\"ppid\":" << ppid << ",";
    output << "\"user\":" << quoteJsonString(user) << ",";
    output << "\"name\":" << quoteJsonString(name) << ",";
    output << "\"args\":" << quoteJsonString(args);
    output << "}";
    return output.str();
}

std::string parseProcessArray(const std::string& text) {
    std::ostringstream output;
    output << "[";
    bool first = true;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        std::string process = parseProcessLineOrEmpty(line);
        if (process.empty()) {
            continue;
        }
        if (!first) {
            output << ",";
        }
        first = false;
        output << process;
    }
    output << "]";
    return output.str();
}

long long parseLeadingLong(const std::string& value, long long defaultValue) {
    std::string clean = trim(value);
    if (clean.empty()) {
        return defaultValue;
    }

    size_t end = 0;
    while (end < clean.size()) {
        char ch = clean[end];
        if ((ch >= '0' && ch <= '9') || (end == 0 && ch == '-')) {
            ++end;
            continue;
        }
        break;
    }
    if (end == 0 || (end == 1 && clean[0] == '-')) {
        return defaultValue;
    }

    long long result = defaultValue;
    if (!parseLongLong(clean.substr(0, end), &result)) {
        return defaultValue;
    }
    return result;
}

std::string parseProcessStats(const std::string& text) {
    if (trim(text).empty()) {
        throw CommandError(-32000, "root process stats output is empty");
    }

    std::ostringstream output;
    output << "{";
    bool first = true;
    bool hasIdentity = false;

    auto putString = [&](const std::string& key, const std::string& value) {
        if (!first) {
            output << ",";
        }
        first = false;
        output << quoteJsonString(key) << ":" << quoteJsonString(value);
        if (key == "name") {
            hasIdentity = true;
        }
    };

    auto putLong = [&](const std::string& key, long long value) {
        if (!first) {
            output << ",";
        }
        first = false;
        output << quoteJsonString(key) << ":" << value;
        if (key == "pid") {
            hasIdentity = true;
        }
    };

    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        size_t separator = line.find(':');
        if (separator == std::string::npos || separator == 0) {
            continue;
        }

        std::string key = trim(line.substr(0, separator));
        std::string value = trim(line.substr(separator + 1));
        if (key == "Name") {
            putString("name", value);
        } else if (key == "State") {
            putString("state", value);
        } else if (key == "Pid") {
            putLong("pid", parseLeadingLong(value, 0));
        } else if (key == "PPid") {
            putLong("ppid", parseLeadingLong(value, 0));
        } else if (key == "TracerPid") {
            putLong("tracerPid", parseLeadingLong(value, 0));
        } else if (key == "Uid") {
            putLong("uid", parseLeadingLong(value, -1));
        } else if (key == "Gid") {
            putLong("gid", parseLeadingLong(value, -1));
        } else if (key == "Threads") {
            putLong("threads", parseLeadingLong(value, 0));
        } else if (key == "VmPeak") {
            putLong("vmPeakKb", parseLeadingLong(value, 0));
        } else if (key == "VmSize") {
            putLong("vmSizeKb", parseLeadingLong(value, 0));
        } else if (key == "VmRSS") {
            putLong("vmRssKb", parseLeadingLong(value, 0));
        } else if (key == "RssAnon") {
            putLong("rssAnonKb", parseLeadingLong(value, 0));
        } else if (key == "RssFile") {
            putLong("rssFileKb", parseLeadingLong(value, 0));
        } else if (key == "RssShmem") {
            putLong("rssShmemKb", parseLeadingLong(value, 0));
        } else if (key == "VmData") {
            putLong("vmDataKb", parseLeadingLong(value, 0));
        } else if (key == "VmStk") {
            putLong("vmStackKb", parseLeadingLong(value, 0));
        } else if (key == "VmExe") {
            putLong("vmExeKb", parseLeadingLong(value, 0));
        } else if (key == "VmLib") {
            putLong("vmLibKb", parseLeadingLong(value, 0));
        } else if (key == "voluntary_ctxt_switches") {
            putLong("voluntaryContextSwitches", parseLeadingLong(value, 0));
        } else if (key == "nonvoluntary_ctxt_switches") {
            putLong("nonvoluntaryContextSwitches", parseLeadingLong(value, 0));
        }
    }

    output << "}";
    if (!hasIdentity) {
        throw CommandError(-32000, "root process stats output is invalid");
    }
    return output.str();
}

std::string parseAppComponent(const std::string& text) {
    std::string component = trim(text);
    size_t separator = component.find('/');
    if (separator == std::string::npos || separator == 0 || separator + 1 >= component.size()) {
        throw CommandError(-32000, "current app output is invalid");
    }

    std::ostringstream output;
    output << "{";
    output << "\"component\":" << quoteJsonString(component) << ",";
    output << "\"packageName\":" << quoteJsonString(component.substr(0, separator)) << ",";
    output << "\"activityName\":" << quoteJsonString(component.substr(separator + 1));
    output << "}";
    return output.str();
}

std::string requireRootSuccess(const RootExecResult& result, const char* defaultError) {
    if (!result.success) {
        throw CommandError(-32000, rootResultError(result, defaultError));
    }
    return "";
}

std::string okBooleanObject() {
    return "{\"ok\":true}";
}

std::string imageMetadataJson(const ImageMetadata& metadata) {
    std::ostringstream output;
    output << "{";
    output << "\"id\":" << metadata.id << ",";
    output << "\"type\":\"image\",";
    output << "\"width\":" << metadata.width << ",";
    output << "\"height\":" << metadata.height << ",";
    output << "\"rowStride\":" << metadata.rowStride << ",";
    output << "\"pixelStride\":" << metadata.pixelStride << ",";
    output << "\"byteLength\":" << metadata.byteLength << ",";
    output << "\"format\":" << quoteJsonString(metadata.format) << ",";
    output << "\"source\":" << quoteJsonString(metadata.source) << ",";
    output << "\"captureDurationMs\":" << metadata.captureDurationMs;
    output << "}";
    return output.str();
}

std::string captureResultJson(ScreenCaptureResult capture, const char* defaultError) {
    if (!capture.success) {
        throw CommandError(-32000, capture.error.empty() ? defaultError : capture.error);
    }

    ImageFrame frame;
    frame.width = capture.width;
    frame.height = capture.height;
    frame.rowStride = capture.rowStride;
    frame.pixelStride = capture.pixelStride;
    frame.format = capture.format;
    frame.source = capture.source;
    frame.captureDurationMs = capture.captureDurationMs;
    frame.pixels = std::move(capture.pixels);

    return imageMetadataJson(storeImageFrame(std::move(frame)));
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
        if (!SystemApi::setRootModeEnabled(enabled)) {
            throw CommandError(-32000, "set root mode failed");
        }
        if (enabled && SystemApi::prepareRootRuntime()) {
            SystemApi::prepareRootHelper();
        }
        return makeDeviceInfoJson();
    }

    if (method == "device.screenState") {
        RootExecResult result = SystemApi::deviceScreenState();
        requireRootSuccess(result, "device screen state failed");
        return parseKeyValueObject(result.stdoutText);
    }

    if (method == "device.wake") {
        RootExecResult result = SystemApi::deviceWake();
        requireRootSuccess(result, "device wake failed");
        return okBooleanObject();
    }

    if (method == "device.sleep") {
        RootExecResult result = SystemApi::deviceSleep();
        requireRootSuccess(result, "device sleep failed");
        return okBooleanObject();
    }

    if (method == "device.battery") {
        RootExecResult result = SystemApi::deviceBattery();
        requireRootSuccess(result, "device battery failed");
        return parseKeyValueObject(result.stdoutText);
    }

    if (method == "device.rotation") {
        RootExecResult result = SystemApi::deviceRotation();
        requireRootSuccess(result, "device rotation failed");
        return parseKeyValueObject(result.stdoutText);
    }

    if (method == "device.setRotation") {
        RootExecResult result = SystemApi::deviceSetRotation(
                requireInt(params, "rotation"),
                params.boolOr("locked", true)
        );
        requireRootSuccess(result, "device set rotation failed");
        return okBooleanObject();
    }

    if (method == "device.settings.get") {
        RootExecResult result = SystemApi::deviceSettingsGet(
                requireSettingsNamespace(params),
                requireKey(params)
        );
        requireRootSuccess(result, "device settings get failed");
        return "{\"value\":" + quoteJsonString(trim(result.stdoutText)) + "}";
    }

    if (method == "device.settings.put") {
        RootExecResult result = SystemApi::deviceSettingsPut(
                requireSettingsNamespace(params),
                requireKey(params),
                requireValue(params)
        );
        requireRootSuccess(result, "device settings put failed");
        return okBooleanObject();
    }

    if (method == "device.settings.delete") {
        RootExecResult result = SystemApi::deviceSettingsDelete(
                requireSettingsNamespace(params),
                requireKey(params)
        );
        requireRootSuccess(result, "device settings delete failed");
        return okBooleanObject();
    }

    if (method == "device.prop.get") {
        RootExecResult result = SystemApi::devicePropGet(requireKey(params));
        requireRootSuccess(result, "device prop get failed");
        return "{\"value\":" + quoteJsonString(trim(result.stdoutText)) + "}";
    }

    if (method == "device.prop.set") {
        RootExecResult result = SystemApi::devicePropSet(requireKey(params), requireValue(params));
        requireRootSuccess(result, "device prop set failed");
        return okBooleanObject();
    }

    if (method == "device.display.info") {
        RootExecResult result = SystemApi::deviceDisplayInfo();
        requireRootSuccess(result, "device display info failed");
        return parseKeyValueObject(result.stdoutText);
    }

    if (method == "device.display.setSize") {
        RootExecResult result = SystemApi::deviceDisplaySetSize(
                requirePositiveInt(params, "width"),
                requirePositiveInt(params, "height")
        );
        requireRootSuccess(result, "device display set size failed");
        return okBooleanObject();
    }

    if (method == "device.display.resetSize") {
        RootExecResult result = SystemApi::deviceDisplayResetSize();
        requireRootSuccess(result, "device display reset size failed");
        return okBooleanObject();
    }

    if (method == "device.display.setDensity") {
        RootExecResult result = SystemApi::deviceDisplaySetDensity(requirePositiveInt(params, "density"));
        requireRootSuccess(result, "device display set density failed");
        return okBooleanObject();
    }

    if (method == "device.display.resetDensity") {
        RootExecResult result = SystemApi::deviceDisplayResetDensity();
        requireRootSuccess(result, "device display reset density failed");
        return okBooleanObject();
    }

    if (method == "device.display.setBrightness") {
        RootExecResult result = SystemApi::deviceDisplaySetBrightness(requireInt(params, "brightness"));
        requireRootSuccess(result, "device display set brightness failed");
        return okBooleanObject();
    }

    if (method == "device.display.setAutoBrightness") {
        RootExecResult result = SystemApi::deviceDisplaySetAutoBrightness(requireBool(params, "enabled"));
        requireRootSuccess(result, "device display set auto brightness failed");
        return okBooleanObject();
    }

    if (method == "root.exec") {
        std::string command = requireString(params, "command");
        return makeRootExecResultJson(SystemApi::rootExec(command, params.intOr("timeoutMs", 2500)));
    }

    if (method == "root.status") {
        return makeRootStatusJson(SystemApi::rootStatus());
    }

    if (method == "root.file.exists") {
        RootExecResult result = SystemApi::rootFileExists(requirePath(params));
        return "{\"exists\":" + boolText(result.success)
                + ",\"error\":"
                + quoteJsonString(result.error)
                + "}";
    }

    if (method == "root.file.readText") {
        RootExecResult result = SystemApi::rootFileReadText(
                requirePath(params),
                params.intOr("timeoutMs", 2500)
        );
        requireRootSuccess(result, "root file read failed");
        return "{\"content\":" + quoteJsonString(result.stdoutText) + "}";
    }

    if (method == "root.file.writeText") {
        RootExecResult result = SystemApi::rootFileWriteText(
                requirePath(params),
                requireStringAllowEmpty(params, "content"),
                params.intOr("timeoutMs", 2500)
        );
        requireRootSuccess(result, "root file write failed");
        return okBooleanObject();
    }

    if (method == "root.file.stat") {
        RootExecResult result = SystemApi::rootFileStat(requirePath(params));
        requireRootSuccess(result, "root file stat failed");
        return "{\"file\":" + parseRootFileInfo(result.stdoutText) + "}";
    }

    if (method == "root.file.list") {
        RootExecResult result = SystemApi::rootFileList(requirePath(params));
        requireRootSuccess(result, "root file list failed");
        return "{\"entries\":" + parseRootFileInfoArray(result.stdoutText) + "}";
    }

    if (method == "root.file.remove") {
        RootExecResult result = SystemApi::rootFileRemove(
                requirePath(params),
                params.boolOr("recursive", false)
        );
        requireRootSuccess(result, "root file remove failed");
        return okBooleanObject();
    }

    if (method == "root.file.mkdir") {
        RootExecResult result = SystemApi::rootFileMkdir(
                requirePath(params),
                params.boolOr("recursive", true)
        );
        requireRootSuccess(result, "root file mkdir failed");
        return okBooleanObject();
    }

    if (method == "root.file.chmod") {
        RootExecResult result = SystemApi::rootFileChmod(requirePath(params), requireString(params, "mode"));
        requireRootSuccess(result, "root file chmod failed");
        return okBooleanObject();
    }

    if (method == "root.file.chown") {
        RootExecResult result = SystemApi::rootFileChown(requirePath(params), requireString(params, "owner"));
        requireRootSuccess(result, "root file chown failed");
        return okBooleanObject();
    }

    if (method == "root.process.pidOf") {
        RootExecResult result = SystemApi::rootProcessPidOf(requireProcessName(params));
        requireRootSuccess(result, "root process pidOf failed");
        return "{\"pids\":" + parsePidArray(result.stdoutText) + "}";
    }

    if (method == "root.process.list") {
        RootExecResult result = SystemApi::rootProcessList();
        requireRootSuccess(result, "root process list failed");
        return "{\"processes\":" + parseProcessArray(result.stdoutText) + "}";
    }

    if (method == "root.process.info") {
        RootExecResult result = SystemApi::rootProcessInfo(requireProcessTarget(params));
        requireRootSuccess(result, "root process info failed");
        return "{\"processes\":" + parseProcessArray(result.stdoutText) + "}";
    }

    if (method == "root.process.stats") {
        RootExecResult result = SystemApi::rootProcessStats(requireProcessTarget(params));
        requireRootSuccess(result, "root process stats failed");
        return parseProcessStats(result.stdoutText);
    }

    if (method == "root.process.kill") {
        RootExecResult result = SystemApi::rootProcessKill(
                requireProcessTarget(params),
                params.intOr("signal", 15)
        );
        requireRootSuccess(result, "root process kill failed");
        return okBooleanObject();
    }

    if (method == "app.isInstalled") {
        return "{\"installed\":" + boolText(SystemApi::appIsInstalled(requirePackageName(params))) + "}";
    }

    if (method == "app.open" || method == "app.start") {
        return "{\"ok\":" + boolText(SystemApi::appOpen(requirePackageName(params))) + "}";
    }

    if (method == "app.stop") {
        return "{\"ok\":" + boolText(SystemApi::appStop(requirePackageName(params))) + "}";
    }

    if (method == "app.clearData") {
        return "{\"ok\":" + boolText(SystemApi::appClearData(requirePackageName(params))) + "}";
    }

    if (method == "app.grant") {
        return "{\"ok\":"
                + boolText(SystemApi::appGrantPermission(
                        requirePackageName(params),
                        requirePermissionName(params)))
                + "}";
    }

    if (method == "app.revoke") {
        return "{\"ok\":"
                + boolText(SystemApi::appRevokePermission(
                        requirePackageName(params),
                        requirePermissionName(params)))
                + "}";
    }

    if (method == "app.current") {
        RootExecResult result = SystemApi::appCurrent();
        requireRootSuccess(result, "current app failed");
        return parseAppComponent(result.stdoutText);
    }

    if (method == "app.install") {
        return "{\"ok\":"
                + boolText(SystemApi::appInstall(
                        requireApkPath(params),
                        params.boolOr("replace", true)))
                + "}";
    }

    if (method == "app.uninstall") {
        return "{\"ok\":"
                + boolText(SystemApi::appUninstall(
                        requirePackageName(params),
                        params.boolOr("keepData", false)))
                + "}";
    }

    if (method == "app.disable") {
        return "{\"ok\":" + boolText(SystemApi::appDisable(requirePackageName(params))) + "}";
    }

    if (method == "app.enable") {
        return "{\"ok\":" + boolText(SystemApi::appEnable(requirePackageName(params))) + "}";
    }

    if (method == "app.disableComponent") {
        return "{\"ok\":"
                + boolText(SystemApi::appDisableComponent(requireComponentName(params)))
                + "}";
    }

    if (method == "app.enableComponent") {
        return "{\"ok\":"
                + boolText(SystemApi::appEnableComponent(requireComponentName(params)))
                + "}";
    }

    if (method == "key.press") {
        int keyCode = requireInt(params, "keyCode");
        if (keyCode < 0) {
            throw CommandError(-32602, "keyCode is required");
        }
        return "{\"ok\":" + boolText(SystemApi::keyPress(keyCode)) + "}";
    }

    if (method == "input.text") {
        return "{\"ok\":" + boolText(SystemApi::inputText(requireStringAllowEmpty(params, "text"))) + "}";
    }

    if (method == "input.pasteText") {
        return "{\"ok\":"
                + boolText(SystemApi::pasteText(requireStringAllowEmpty(params, "text")))
                + "}";
    }

    if (method == "script.run") {
        std::string language = params.stringOr("language", "lua");
        if (language != "lua") {
            throw CommandError(-32602, "only lua is supported now");
        }

        std::string code = requireString(params, "code");
        std::string message = engine.runLuaText((luaRuntimeBootstrap + "\n" + code).c_str());
        if (message == "Engine is already running") {
            throw CommandError(-32000, "已有脚本正在运行");
        }

        std::string statusJson = engine.statusJson(0);
        JsonValue status;
        std::string parseError;
        if (!parseJsonText(statusJson, &status, &parseError) || !status.isObject()) {
            throw CommandError(-32000, "script status parse failed");
        }

        std::ostringstream output;
        output << "{";
        output << "\"taskId\":" << status.intOr("taskId", 0) << ",";
        output << "\"message\":" << quoteJsonString(message) << ",";
        output << "\"status\":" << quoteJsonString(status.stringOr("status", "unknown"));
        output << "}";
        return output.str();
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

    if (method == "log.drain") {
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

    if (method == "script.status") {
        return engine.statusJson(params.intOr("taskId", 0));
    }

    if (method == "screen.capture") {
        return captureResultJson(SystemApi::captureScreen(), "screen capture failed");
    }

    if (method == "root.screen.capture") {
        return captureResultJson(SystemApi::captureRootScreen(), "root screen capture failed");
    }

    if (method == "image.release") {
        int imageId = params.intOr("id", 0);
        if (imageId <= 0) {
            imageId = params.intOr("imageId", 0);
        }
        if (imageId <= 0) {
            throw CommandError(-32602, "image id is required");
        }
        if (!releaseImageFrame(imageId)) {
            throw CommandError(-32602, "image handle is not found");
        }
        return "{\"released\":true}";
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
