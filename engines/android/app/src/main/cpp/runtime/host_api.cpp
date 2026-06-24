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

std::string rootResultError(const RootExecResult& result, const char* fallback) {
    if (!result.error.empty()) {
        return result.error;
    }
    if (!result.stderrText.empty()) {
        return result.stderrText;
    }
    return fallback;
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

int luaRootFileRemove(lua_State* state) {
    const char* path = luaL_checkstring(state, 1);
    RootExecResult result = AndroidBridge::rootFileRemove(path);
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
    lua_setfield(state, hostTableIndex, "device");

    lua_newtable(state);
    int rootTableIndex = lua_gettop(state);
    setFunctionField(state, rootTableIndex, "exec", luaRootExec);
    setFunctionField(state, rootTableIndex, "isAvailable", luaDeviceIsRootAvailable);

    lua_newtable(state);
    int rootFileTableIndex = lua_gettop(state);
    setFunctionField(state, rootFileTableIndex, "exists", luaRootFileExists);
    setFunctionField(state, rootFileTableIndex, "readText", luaRootFileReadText);
    setFunctionField(state, rootFileTableIndex, "writeText", luaRootFileWriteText);
    setFunctionField(state, rootFileTableIndex, "remove", luaRootFileRemove);
    setFunctionField(state, rootFileTableIndex, "mkdir", luaRootFileMkdir);
    setFunctionField(state, rootFileTableIndex, "chmod", luaRootFileChmod);
    lua_setfield(state, rootTableIndex, "file");

    lua_newtable(state);
    int rootProcessTableIndex = lua_gettop(state);
    setFunctionField(state, rootProcessTableIndex, "pidOf", luaRootProcessPidOf);
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
