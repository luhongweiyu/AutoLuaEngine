/**
 * 文件用途：注册 Lua HostApi；固定设备能力调用 C ABI，Lua 线程能力接入本运行时调度器。
 */
#include "host_api.h"

#include <cstdint>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "../../core/system_c_api.h"
#include "../../engine/json_value.h"
#include "java_bridge.h"
#include "lua_runtime.h"
#include "lua_thread_api.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

namespace {

/**
 * 把 Lua 任意值转成字符串，保持 print(...) 和 Lua 原生 __tostring 行为一致。
 */
std::string luaValueToString(lua_State* state, int index) {
    luaL_tolstring(state, index, nullptr);
    const char* text = lua_tostring(state, -1);
    std::string result = text == nullptr ? "" : text;
    lua_pop(state, 1);
    return result;
}

int luaShouldInterrupt(void* context) {
    auto* runtime = static_cast<LuaRuntime*>(context);
    return runtime != nullptr && runtime->shouldInterruptNow() ? 1 : 0;
}

void setFunctionField(lua_State* state, int tableIndex, const char* name, lua_CFunction function) {
    lua_pushcfunction(state, function);
    lua_setfield(state, tableIndex, name);
}

int luaCheckInt(lua_State* state, int index, const char* name) {
    lua_Integer value = luaL_checkinteger(state, index);
    if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) {
        luaL_error(state, "%s 超出整数范围", name);
        return 0;
    }
    return static_cast<int>(value);
}

/**
 * 将 Lua 值序列化为 JSON。
 *
 * 脚本 UI 的配置在 Lua 中自然地写成 table，而稳定 C ABI 只接收 JSON 文本。这里的
 * 转换仅属于 Lua 绑定层，不把 Lua table 或 userdata 泄露到 core/api。连续的 1..n
 * 整数键 table 输出 JSON 数组，其余 table 输出 JSON 对象。
 */
bool luaValueToJson(
        lua_State* state,
        int index,
        std::string* output,
        std::string* error,
        int depth,
        std::unordered_set<const void*>* visitingTables
);

bool isLuaSequenceTable(lua_State* state, int index, size_t* length) {
    int absoluteIndex = lua_absindex(state, index);
    size_t sequenceLength = lua_rawlen(state, absoluteIndex);
    bool isSequence = true;

    lua_pushnil(state);
    while (lua_next(state, absoluteIndex) != 0) {
        if (!lua_isinteger(state, -2)) {
            isSequence = false;
        } else {
            lua_Integer key = lua_tointeger(state, -2);
            if (key < 1 || static_cast<size_t>(key) > sequenceLength) {
                isSequence = false;
            }
        }
        lua_pop(state, 1);
        if (!isSequence) {
            // 提前结束 lua_next 时栈上仍保留当前 key，需要主动弹出。
            lua_pop(state, 1);
            break;
        }
    }

    if (length != nullptr) {
        *length = sequenceLength;
    }
    return isSequence && sequenceLength > 0;
}

bool luaTableToJson(
        lua_State* state,
        int index,
        std::string* output,
        std::string* error,
        int depth,
        std::unordered_set<const void*>* visitingTables
) {
    if (depth > 32) {
        *error = "UI 配置表嵌套层级过深";
        return false;
    }

    int absoluteIndex = lua_absindex(state, index);
    const void* tablePointer = lua_topointer(state, absoluteIndex);
    if (tablePointer != nullptr && !visitingTables->insert(tablePointer).second) {
        *error = "UI 配置表不能循环引用";
        return false;
    }

    size_t sequenceLength = 0;
    bool isSequence = isLuaSequenceTable(state, absoluteIndex, &sequenceLength);
    std::ostringstream json;
    if (isSequence) {
        json << "[";
        for (size_t i = 1; i <= sequenceLength; ++i) {
            lua_rawgeti(state, absoluteIndex, static_cast<lua_Integer>(i));
            std::string item;
            bool ok = luaValueToJson(state, -1, &item, error, depth + 1, visitingTables);
            lua_pop(state, 1);
            if (!ok) {
                if (tablePointer != nullptr) {
                    visitingTables->erase(tablePointer);
                }
                return false;
            }
            if (i > 1) {
                json << ",";
            }
            json << item;
        }
        json << "]";
    } else {
        std::map<std::string, std::string> objectItems;
        lua_pushnil(state);
        while (lua_next(state, absoluteIndex) != 0) {
            std::string key;
            if (lua_type(state, -2) == LUA_TSTRING) {
                const char* text = lua_tostring(state, -2);
                key = text == nullptr ? "" : text;
            } else if (lua_isinteger(state, -2)) {
                key = std::to_string(lua_tointeger(state, -2));
            } else {
                lua_pop(state, 1);
                if (tablePointer != nullptr) {
                    visitingTables->erase(tablePointer);
                }
                *error = "UI 配置表的键只能是字符串或整数";
                return false;
            }

            std::string item;
            bool ok = luaValueToJson(state, -1, &item, error, depth + 1, visitingTables);
            lua_pop(state, 1);
            if (!ok) {
                if (tablePointer != nullptr) {
                    visitingTables->erase(tablePointer);
                }
                return false;
            }
            objectItems[key] = item;
        }

        json << "{";
        bool first = true;
        for (const auto& item : objectItems) {
            if (!first) {
                json << ",";
            }
            first = false;
            json << quoteJsonString(item.first) << ":" << item.second;
        }
        json << "}";
    }

    if (tablePointer != nullptr) {
        visitingTables->erase(tablePointer);
    }
    *output = json.str();
    return true;
}

bool luaValueToJson(
        lua_State* state,
        int index,
        std::string* output,
        std::string* error,
        int depth,
        std::unordered_set<const void*>* visitingTables
) {
    switch (lua_type(state, index)) {
        case LUA_TNIL:
            *output = "null";
            return true;
        case LUA_TBOOLEAN:
            *output = lua_toboolean(state, index) ? "true" : "false";
            return true;
        case LUA_TNUMBER: {
            std::ostringstream json;
            if (lua_isinteger(state, index)) {
                json << lua_tointeger(state, index);
            } else {
                lua_Number number = lua_tonumber(state, index);
                if (!std::isfinite(static_cast<double>(number))) {
                    *error = "UI 配置不能包含 NaN 或无穷大";
                    return false;
                }
                json << std::setprecision(15) << static_cast<double>(number);
            }
            *output = json.str();
            return true;
        }
        case LUA_TSTRING: {
            const char* text = lua_tostring(state, index);
            *output = quoteJsonString(text == nullptr ? "" : text);
            return true;
        }
        case LUA_TTABLE:
            return luaTableToJson(state, index, output, error, depth, visitingTables);
        default:
            *error = "UI 配置只支持空值、布尔值、数字、字符串和表";
            return false;
    }
}

bool luaArgumentToJson(lua_State* state, int index, std::string* output, std::string* error) {
    std::unordered_set<const void*> visitingTables;
    return luaValueToJson(state, index, output, error, 0, &visitingTables);
}

/**
 * 将 native UI 事件 JSON 转为 Lua 值。
 */
void pushJsonValueToLua(lua_State* state, const JsonValue& value, int depth) {
    if (depth > 32) {
        lua_pushnil(state);
        return;
    }

    if (value.isNull()) {
        lua_pushnil(state);
        return;
    }
    if (value.isBool()) {
        lua_pushboolean(state, value.boolValue());
        return;
    }
    if (value.isNumber()) {
        lua_pushnumber(state, static_cast<lua_Number>(value.numberValue()));
        return;
    }
    if (value.isString()) {
        lua_pushlstring(state, value.stringValue().c_str(), value.stringValue().size());
        return;
    }
    if (value.isArray()) {
        const std::vector<JsonValue>& array = value.arrayValue();
        lua_createtable(state, static_cast<int>(array.size()), 0);
        for (size_t index = 0; index < array.size(); ++index) {
            pushJsonValueToLua(state, array[index], depth + 1);
            lua_rawseti(state, -2, static_cast<lua_Integer>(index + 1));
        }
        return;
    }

    const std::map<std::string, JsonValue>& object = value.objectValue();
    lua_createtable(state, 0, static_cast<int>(object.size()));
    for (const auto& item : object) {
        pushJsonValueToLua(state, item.second, depth + 1);
        lua_setfield(state, -2, item.first.c_str());
    }
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

    engine_print(output.str().c_str());
    return 0;
}

int luaSleep(lua_State* state) {
    lua_Integer duration = luaL_checkinteger(state, 1);
    if (duration < 0) {
        return luaL_error(state, "休眠时间必须大于等于 0 毫秒");
    }
    if (duration > std::numeric_limits<int>::max()) {
        return luaL_error(state, "休眠时间过大");
    }

    lua_getfield(state, LUA_REGISTRYINDEX, "小鱼精灵Runtime");
    LuaRuntime* runtime = static_cast<LuaRuntime*>(lua_touserdata(state, -1));
    lua_pop(state, 1);

    // sleep 是自动化脚本最常见的等待点。按短片段调用统一 C ABI，并在片段之间
    // 处理 Java 监听器队列，使异步 Sensor/Runnable 等回调无需并发操作 Lua VM。
    auto deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(static_cast<long long>(duration));
    do {
        processLuaJavaCallbacks(state);
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()
        ).count();
        if (remaining <= 0) {
            break;
        }

        int sliceMs = static_cast<int>(remaining > 20 ? 20 : remaining);
        // duration 和 runtime 已经复制到 native 局部变量，此后不再访问 Lua 栈。释放
        // VM Gate 后，其他真实子线程可以在当前任务休眠期间执行 Lua 字节码。
        bool released = runtime != nullptr && runtime->releaseVmForBlocking();
        bool slept = engine_sleepInterruptible(sliceMs, luaShouldInterrupt, runtime) != 0;
        if (runtime != nullptr) {
            runtime->reacquireVmAfterBlocking(released);
        }
        if (!slept) {
            const char* error = engine_runtimeLastError();
            return luaL_error(
                    state,
                    error == nullptr || error[0] == '\0' ? "脚本已停止" : error
            );
        }
    } while (true);

    if (runtime != nullptr && runtime->shouldInterruptNow()) {
        const char* error = engine_runtimeLastError();
        return luaL_error(
                state,
                error == nullptr || error[0] == '\0' ? "脚本已停止" : error
        );
    }

    lua_pushboolean(state, 1);
    return 1;
}

int luaLogPrint(lua_State* state) {
    const char* text = luaL_checkstring(state, 1);
    engine_logPrint(text == nullptr ? "" : text);
    lua_pushboolean(state, 1);
    return 1;
}

/**
 * Lua 系统时间入口。
 *
 * 返回 Unix epoch 毫秒时间戳。真实取值由 libengine.so C ABI 提供，Lua 层
 * 只负责把 64 位整数压回 Lua 栈。
 */
int luaSystemTime(lua_State* state) {
    lua_pushinteger(state, static_cast<lua_Integer>(engine_systemTime()));
    return 1;
}

/**
 * Lua 脚本运行时间入口。
 *
 * 返回当前脚本从开始执行到现在的毫秒数。计时起点由 LuaRuntime 在 lua_pcall
 * 之前写入 runtime_api。
 */
int luaTickCount(lua_State* state) {
    lua_pushinteger(state, static_cast<lua_Integer>(engine_tickCount()));
    return 1;
}

/**
 * 读取当前 ALPKG 中声明为 resource 的原始文件。
 *
 * Lua string 本身记录长度，能够无损承载 CSS、JSON、图片等含零字节的二进制内容。
 * 绑定层不解析 ZIP、不回退读取包外文件，所有路径和类型校验统一由 libengine.so C ABI
 * 完成，使未来 JS/Go 使用同一 ABI 时与 Lua 具有完全一致的语义。
 */
int luaReadAlpkgFile(lua_State* state) {
    size_t pathLength = 0;
    const char* relativePath = luaL_checklstring(state, 1, &pathLength);
    if (relativePath == nullptr || pathLength == 0) {
        return luaL_argerror(state, 1, "ALPKG 资源路径不能为空");
    }
    if (std::strlen(relativePath) != pathLength) {
        return luaL_argerror(state, 1, "ALPKG 资源路径不能包含零字节");
    }

    const unsigned char* data = nullptr;
    size_t dataSize = 0;
    if (!engine_readAlpkgFile(relativePath, &data, &dataSize)) {
        const char* error = engine_runtimeLastError();
        lua_pushnil(state);
        lua_pushstring(state, error == nullptr || error[0] == '\0' ? "读取 ALPKG 资源失败" : error);
        return 2;
    }

    lua_pushlstring(state, reinterpret_cast<const char*>(data), dataSize);
    return 1;
}

const char* luaKeyCodeToString(lua_State* state, int index) {
    if (lua_isinteger(state, index)) {
        lua_pushfstring(state, "%I", lua_tointeger(state, index));
        return lua_tostring(state, -1);
    }
    return luaL_checkstring(state, index);
}

int luaTouchDown(lua_State* state) {
    int id = luaCheckInt(state, 1, "id");
    int x = luaCheckInt(state, 2, "x");
    int y = luaCheckInt(state, 3, "y");
    engine_touchDown(id, x, y);
    return 0;
}

int luaTouchMove(lua_State* state) {
    int id = luaCheckInt(state, 1, "id");
    int x = luaCheckInt(state, 2, "x");
    int y = luaCheckInt(state, 3, "y");
    lua_pushboolean(state, engine_touchMove(id, x, y));
    return 1;
}

int luaTouchUp(lua_State* state) {
    int id = luaCheckInt(state, 1, "id");
    engine_touchUp(id);
    return 0;
}

int luaKeyDown(lua_State* state) {
    const char* keyCode = luaKeyCodeToString(state, 1);
    bool ok = engine_keyDown(keyCode) != 0;
    if (lua_isinteger(state, 1)) {
        lua_pop(state, 1);
    }
    lua_pushboolean(state, ok);
    return 1;
}

int luaKeyUp(lua_State* state) {
    const char* keyCode = luaKeyCodeToString(state, 1);
    bool ok = engine_keyUp(keyCode) != 0;
    if (lua_isinteger(state, 1)) {
        lua_pop(state, 1);
    }
    lua_pushboolean(state, ok);
    return 1;
}

int luaKeyPress(lua_State* state) {
    const char* keyCode = luaKeyCodeToString(state, 1);
    bool ok = engine_keyPress(keyCode) != 0;
    if (lua_isinteger(state, 1)) {
        lua_pop(state, 1);
    }
    lua_pushboolean(state, ok);
    return 1;
}

int luaInputText(lua_State* state) {
    const char* text = luaL_checkstring(state, 1);
    lua_pushboolean(state, engine_inputText(text));
    return 1;
}

/**
 * Lua 输入法锁定入口。
 *
 * 固定 Lua API 只做返回值转换；保存原输入法和系统切换由 libengine.so 的 C ABI
 * 与 Android Root helper 完成。
 */
int luaImeLock(lua_State* state) {
    lua_pushboolean(state, engine_imeLock());
    return 1;
}

/**
 * Lua 输入法文本提交入口。
 */
int luaImeSetText(lua_State* state) {
    const char* text = luaL_checkstring(state, 1);
    lua_pushboolean(state, engine_imeSetText(text));
    return 1;
}

/**
 * Lua 输入法解锁入口。
 */
int luaImeUnlock(lua_State* state) {
    lua_pushboolean(state, engine_imeUnlock());
    return 1;
}

int luaGetRunEnvType(lua_State* state) {
    // m.getRunEnvType 按设备方法契约返回整数：0 为 Root，1 为无障碍，-1 为未就绪。
    lua_pushinteger(state, static_cast<lua_Integer>(engine_getRunEnvTypeCode()));
    return 1;
}

/**
 * 将设备 C ABI 返回的 JSON 对象或数组转换为 Lua table。
 *
 * 已安装应用、传感器和屏幕详情在 C ABI 中必须保持 JSON，避免 Lua table 进入跨语言 ABI；
 * 只有 Lua HostApi 在最终边界处执行这一次转换。
 */
int pushDeviceJsonToLua(lua_State* state, const char* jsonText) {
    if (jsonText == nullptr || jsonText[0] == '\0') {
        lua_pushnil(state);
        return 1;
    }

    JsonValue value;
    std::string error;
    if (!parseJsonText(jsonText, &value, &error)) {
        lua_pushnil(state);
        return 1;
    }
    pushJsonValueToLua(state, value, 0);
    return 1;
}

/** 返回设备字符串；Android 没有公开该字段时按 Lua 约定返回 nil。 */
int pushDeviceStringToLua(lua_State* state, const char* value) {
    if (value == nullptr) {
        lua_pushnil(state);
    } else {
        lua_pushstring(state, value);
    }
    return 1;
}

int luaAppIsFront(lua_State* state) {
    const char* packageName = luaL_checkstring(state, 1);
    lua_pushboolean(state, engine_appIsFront(packageName));
    return 1;
}

int luaAppIsRunning(lua_State* state) {
    const char* packageName = luaL_checkstring(state, 1);
    lua_pushboolean(state, engine_appIsRunning(packageName));
    return 1;
}

int luaFrontAppName(lua_State* state) {
    return pushDeviceStringToLua(state, engine_frontAppName());
}

int luaGetCurrentActivity(lua_State* state) {
    return pushDeviceStringToLua(state, engine_getCurrentActivity());
}

int luaRunApp(lua_State* state) {
    const char* packageName = luaL_checkstring(state, 1);
    const char* componentName = lua_isnoneornil(state, 2) ? nullptr : luaL_checkstring(state, 2);
    bool isOpenBySuper = !lua_isnoneornil(state, 3) && lua_toboolean(state, 3);
    engine_runApp(packageName, componentName, isOpenBySuper ? 1 : 0);
    return 0;
}

int luaStopApp(lua_State* state) {
    engine_stopApp(luaL_checkstring(state, 1));
    return 0;
}

int luaRunIntent(lua_State* state) {
    std::string intentJson;
    std::string error;
    if (!luaArgumentToJson(state, 1, &intentJson, &error)) {
        return luaL_argerror(state, 1, error.c_str());
    }
    lua_pushboolean(state, engine_runIntent(intentJson.c_str()));
    return 1;
}

int luaInstallApk(lua_State* state) {
    engine_installApk(luaL_checkstring(state, 1));
    return 0;
}

int luaGetInstalledApk(lua_State* state) {
    return pushDeviceJsonToLua(state, engine_getInstalledApkJson());
}

int luaGetInstalledApps(lua_State* state) {
    return pushDeviceJsonToLua(state, engine_getInstalledAppsJson());
}

int luaGetInsallAppInfos(lua_State* state) {
    return pushDeviceJsonToLua(state, engine_getInsallAppInfosJson());
}

int luaGetApkVerInt(lua_State* state) {
    lua_pushinteger(state, static_cast<lua_Integer>(engine_getApkVerInt()));
    return 1;
}

int luaExec(lua_State* state) {
    const char* command = luaL_checkstring(state, 1);
    bool isRet = lua_isnoneornil(state, 2) || lua_toboolean(state, 2);
    const char* output = engine_exec(command, isRet ? 1 : 0);
    if (!isRet) {
        return 0;
    }
    return pushDeviceStringToLua(state, output);
}

int luaExitScript(lua_State* state) {
    engine_exitScript();
    // 设置顶层停止位后立即展开当前 Lua 调用栈，避免 exitScript 后继续执行后续语句。
    return luaL_error(state, "脚本已停止");
}

int luaGetXiaoyvApi(lua_State* state) {
    lua_pushinteger(
            state,
            static_cast<lua_Integer>(reinterpret_cast<std::uintptr_t>(engine_getApi()))
    );
    return 1;
}

int luaGetBatteryLevel(lua_State* state) {
    lua_pushinteger(state, static_cast<lua_Integer>(engine_getBatteryLevel()));
    return 1;
}

int luaGetBoard(lua_State* state) {
    return pushDeviceStringToLua(state, engine_getBoard());
}

int luaGetBootLoader(lua_State* state) {
    return pushDeviceStringToLua(state, engine_getBootLoader());
}

int luaGetBrand(lua_State* state) {
    return pushDeviceStringToLua(state, engine_getBrand());
}

int luaGetCpuAbi(lua_State* state) {
    return pushDeviceStringToLua(state, engine_getCpuAbi());
}

int luaGetCpuAbi2(lua_State* state) {
    return pushDeviceStringToLua(state, engine_getCpuAbi2());
}

int luaGetCpuArch(lua_State* state) {
    lua_pushinteger(state, static_cast<lua_Integer>(engine_getCpuArch()));
    return 1;
}

int luaGetDevice(lua_State* state) {
    return pushDeviceStringToLua(state, engine_getDevice());
}

int luaGetDeviceId(lua_State* state) {
    return pushDeviceStringToLua(state, engine_getDeviceId());
}

int luaGetDisplayDpi(lua_State* state) {
    lua_pushinteger(state, static_cast<lua_Integer>(engine_getDisplayDpi()));
    return 1;
}

int luaGetDisplayInfo(lua_State* state) {
    return pushDeviceJsonToLua(state, engine_getDisplayInfoJson());
}

int luaGetDisplayRotate(lua_State* state) {
    lua_pushinteger(state, static_cast<lua_Integer>(engine_getDisplayRotate()));
    return 1;
}

int luaGetDisplaySize(lua_State* state) {
    int width = 0;
    int height = 0;
    if (!engine_getDisplaySize(&width, &height)) {
        lua_pushnil(state);
        return 1;
    }
    lua_pushinteger(state, static_cast<lua_Integer>(width));
    lua_pushinteger(state, static_cast<lua_Integer>(height));
    return 2;
}

int luaGetFingerprint(lua_State* state) {
    return pushDeviceStringToLua(state, engine_getFingerprint());
}

int luaGetHardware(lua_State* state) {
    return pushDeviceStringToLua(state, engine_getHardware());
}

int luaGetId(lua_State* state) {
    return pushDeviceStringToLua(state, engine_getId());
}

int luaGetManufacturer(lua_State* state) {
    return pushDeviceStringToLua(state, engine_getManufacturer());
}

int luaGetModel(lua_State* state) {
    return pushDeviceStringToLua(state, engine_getModel());
}

int luaGetNetWorkTime(lua_State* state) {
    return pushDeviceStringToLua(state, engine_getNetWorkTime());
}

int luaGetOaid(lua_State* state) {
    return pushDeviceStringToLua(state, engine_getOaid());
}

int luaGetOsVersionName(lua_State* state) {
    return pushDeviceStringToLua(state, engine_getOsVersionName());
}

int luaGetPackageName(lua_State* state) {
    return pushDeviceStringToLua(state, engine_getPackageName());
}

int luaGetProduct(lua_State* state) {
    return pushDeviceStringToLua(state, engine_getProduct());
}

int luaGetSdPath(lua_State* state) {
    return pushDeviceStringToLua(state, engine_getSdPath());
}

int luaGetSdkVersion(lua_State* state) {
    lua_pushinteger(state, static_cast<lua_Integer>(engine_getSdkVersion()));
    return 1;
}

int luaGetSensorsInfo(lua_State* state) {
    return pushDeviceJsonToLua(state, engine_getSensorsInfoJson());
}

int luaGetSimSerialNumber(lua_State* state) {
    return pushDeviceStringToLua(state, engine_getSimSerialNumber());
}

int luaGetSubscriberId(lua_State* state) {
    return pushDeviceStringToLua(state, engine_getSubscriberId());
}

int luaGetWifiMac(lua_State* state) {
    return pushDeviceStringToLua(state, engine_getWifiMac());
}

int luaGetWorkPath(lua_State* state) {
    return pushDeviceStringToLua(state, engine_getWorkPath());
}

int luaLockScreen(lua_State*) {
    engine_lockScreen();
    return 0;
}

int luaUnLockScreen(lua_State*) {
    engine_unLockScreen();
    return 0;
}

int luaSetDisplayPowerOff(lua_State* state) {
    engine_setDisplayPowerOff(lua_toboolean(state, 1) ? 1 : 0);
    return 0;
}

int luaSetAirplaneMode(lua_State* state) {
    engine_setAirplaneMode(lua_toboolean(state, 1) ? 1 : 0);
    return 0;
}

int luaSetBTEnable(lua_State* state) {
    engine_setBTEnable(lua_toboolean(state, 1) ? 1 : 0);
    return 0;
}

int luaSetWifiEnable(lua_State* state) {
    engine_setWifiEnable(lua_toboolean(state, 1) ? 1 : 0);
    return 0;
}

int luaPhoneCall(lua_State* state) {
    const char* number = luaL_checkstring(state, 1);
    int callState = lua_isnoneornil(state, 2) ? 0 : luaCheckInt(state, 2, "state");
    engine_phoneCall(number, callState);
    return 0;
}

int luaSendSms(lua_State* state) {
    engine_sendSms(luaL_checkstring(state, 1), luaL_checkstring(state, 2));
    return 0;
}

int luaVibrate(lua_State* state) {
    engine_vibrate(luaCheckInt(state, 1, "durationMs"));
    return 0;
}

/**
 * Lua 获取屏幕像素入口。
 *
 * 成功返回：width, height, pixelsAddress。
 * 失败返回：nil, errorMessage。
 */
int luaGetScreenPixels(lua_State* state) {
    int width = 0;
    int height = 0;
    unsigned char* pixels = nullptr;

    if (!engine_getScreenPixels(&width, &height, &pixels)) {
        lua_pushnil(state);
        lua_pushstring(state, engine_screenLastError());
        return 2;
    }

    lua_pushinteger(state, static_cast<lua_Integer>(width));
    lua_pushinteger(state, static_cast<lua_Integer>(height));
    lua_pushinteger(state, static_cast<lua_Integer>(reinterpret_cast<std::uintptr_t>(pixels)));
    return 3;
}

/** 把普通文件或 ALPKG 资源图片设置为固定活动屏幕，成功返回 true。 */
int luaSetScreenPixels(lua_State* state) {
    const char* imagePath = luaL_checkstring(state, 1);
    if (!engine_setScreenPixels(imagePath)) {
        lua_pushnil(state);
        lua_pushstring(state, engine_screenLastError());
        return 2;
    }
    lua_pushboolean(state, 1);
    return 1;
}

/** 还原系统屏幕点阵；无图片屏幕时重复调用仍返回 true。 */
int luaRestoreScreenPixels(lua_State* state) {
    engine_restoreScreenPixels();
    lua_pushboolean(state, 1);
    return 1;
}

int luaKeepCapture(lua_State* state) {
    engine_keepCapture();
    lua_pushboolean(state, 1);
    return 1;
}

int luaReleaseCapture(lua_State* state) {
    engine_releaseCapture();
    lua_pushboolean(state, 1);
    return 1;
}

int luaSetCaptureCacheMs(lua_State* state) {
    lua_Integer durationMs = luaL_checkinteger(state, 1);
    if (!engine_setCaptureCacheMs(static_cast<int>(durationMs))) {
        lua_pushnil(state);
        lua_pushstring(state, engine_screenLastError());
        return 2;
    }

    lua_pushinteger(state, durationMs);
    return 1;
}

/**
 * Lua 找色入口。
 *
 * 参数：x1, y1, x2, y2, dir, sim, colors。
 * 成功返回：x, y。
 * 失败返回：nil, errorMessage。
 */
int luaColorsFind(lua_State* state) {
    int x1 = luaCheckInt(state, 1, "x1");
    int y1 = luaCheckInt(state, 2, "y1");
    int x2 = luaCheckInt(state, 3, "x2");
    int y2 = luaCheckInt(state, 4, "y2");
    int dir = luaCheckInt(state, 5, "dir");
    int sim = luaCheckInt(state, 6, "sim");
    const char* colors = luaL_checkstring(state, 7);

    EnginePoint point{-1, -1};
    if (!engine_findColors(x1, y1, x2, y2, dir, sim, colors, &point)) {
        lua_pushnil(state);
        lua_pushstring(state, engine_findColorsLastError());
        return 2;
    }

    lua_pushinteger(state, point.x);
    lua_pushinteger(state, point.y);
    return 2;
}

/** 将 C ABI JSON 结果解析为 Lua table；失败时保留 native 中文错误。 */
int pushCAbiJsonOrError(lua_State* state, const char* jsonText, const char* errorText) {
    if (jsonText == nullptr || jsonText[0] == '\0') {
        lua_pushnil(state);
        lua_pushstring(state, errorText == nullptr || errorText[0] == '\0' ? "native 调用失败" : errorText);
        return 2;
    }
    JsonValue value;
    std::string parseError;
    if (!parseJsonText(jsonText, &value, &parseError)) {
        lua_pushnil(state);
        lua_pushstring(state, ("native JSON 返回无效：" + parseError).c_str());
        return 2;
    }
    pushJsonValueToLua(state, value, 0);
    return 1;
}

/** 把可选 Lua table OCR 配置转换为 C ABI JSON 对象。 */
bool luaOptionalOptionsJson(lua_State* state, int index, std::string* output, std::string* error) {
    if (output == nullptr || error == nullptr) {
        return false;
    }
    if (lua_isnoneornil(state, index)) {
        *output = "{}";
        return true;
    }
    if (!lua_istable(state, index)) {
        *error = "options 必须是 table";
        return false;
    }
    return luaArgumentToJson(state, index, output, error);
}

/** 显式把当前截图缓存的全屏或指定区域编码保存为图片文件。 */
int luaCapture(lua_State* state) {
    const char* path = luaL_checkstring(state, 1);
    int argumentCount = lua_gettop(state);
    if (argumentCount != 1 && argumentCount != 5) {
        return luaL_error(state, "capture 需要 path，或 path、left、top、right、bottom 五个参数");
    }

    EngineRect region{};
    const EngineRect* regionPointer = nullptr;
    if (argumentCount == 5) {
        region.left = luaCheckInt(state, 2, "left");
        region.top = luaCheckInt(state, 3, "top");
        region.right = luaCheckInt(state, 4, "right");
        region.bottom = luaCheckInt(state, 5, "bottom");
        regionPointer = &region;
    }

    if (!engine_capture(path, regionPointer)) {
        lua_pushnil(state);
        lua_pushstring(state, engine_imageLastError());
        return 2;
    }
    lua_pushboolean(state, 1);
    return 1;
}

/** Lua 模板找图入口，参数与旧 findPic 保持同一顺序。 */
int luaFindPic(lua_State* state) {
    int x1 = luaCheckInt(state, 1, "x1");
    int y1 = luaCheckInt(state, 2, "y1");
    int x2 = luaCheckInt(state, 3, "x2");
    int y2 = luaCheckInt(state, 4, "y2");
    const char* picName = luaL_checkstring(state, 5);
    const char* deltaColor = luaL_checkstring(state, 6);
    int direction = luaCheckInt(state, 7, "dir");
    double similarity = luaL_checknumber(state, 8);
    EnginePoint point{-1, -1};
    if (!engine_findPic(x1, y1, x2, y2, picName, deltaColor, direction, similarity, &point)) {
        const char* error = engine_imageLastError();
        if (error == nullptr || error[0] == '\0') {
            lua_pushnil(state);
            return 1;
        }
        lua_pushnil(state);
        lua_pushstring(state, error);
        return 2;
    }
    lua_pushinteger(state, point.x);
    lua_pushinteger(state, point.y);
    return 2;
}

/** 清理找图模板缓存，picName 省略时清理所有模板。 */
int luaClearImageCache(lua_State* state) {
    const char* picName = lua_isnoneornil(state, 1) ? nullptr : luaL_checkstring(state, 1);
    engine_clearImageCache(picName);
    lua_pushboolean(state, 1);
    return 1;
}

/** 设置当前脚本任务的找图模板缓存上限，单位字节；0 表示关闭缓存。 */
int luaSetImageCacheMaxBytes(lua_State* state) {
    lua_Integer maxBytes = luaL_checkinteger(state, 1);
    if (maxBytes < 0
            || static_cast<unsigned long long>(maxBytes)
                    > static_cast<unsigned long long>(std::numeric_limits<size_t>::max())) {
        lua_pushnil(state);
        lua_pushstring(state, "图片缓存上限必须是当前平台 size_t 可表示的非负整数");
        return 2;
    }

    engine_setImageCacheMaxBytes(static_cast<size_t>(maxBytes));
    lua_pushinteger(state, maxBytes);
    return 1;
}

/** 显式加载或复用 RapidOCR PP-OCR ONNX 模型。 */
int luaOcrLoad(lua_State* state) {
    std::string name = luaL_checkstring(state, 1);
    std::string detPath = luaL_checkstring(state, 2);
    std::string recPath = luaL_checkstring(state, 3);
    std::string clsPath = lua_isnoneornil(state, 4) ? "" : luaL_checkstring(state, 4);
    std::string keysPath = luaL_checkstring(state, 5);
    int threads = lua_isnoneornil(state, 6) ? 2 : luaCheckInt(state, 6, "threads");

    // ONNX session 首次加载可能持续数秒。参数已复制到 native string，等待期间不会访问 Lua
    // 栈，因此释放 VM Gate 后其余 Lua native 子线程仍可继续执行。
    lua_getfield(state, LUA_REGISTRYINDEX, "小鱼精灵Runtime");
    LuaRuntime* runtime = static_cast<LuaRuntime*>(lua_touserdata(state, -1));
    lua_pop(state, 1);
    bool released = runtime != nullptr && runtime->releaseVmForBlocking();
    bool loaded = engine_ocrLoadModel(
            name.c_str(),
            detPath.c_str(),
            recPath.c_str(),
            clsPath.c_str(),
            keysPath.c_str(),
            threads
    ) != 0;
    std::string nativeError = engine_ocrLastError();
    if (runtime != nullptr) {
        runtime->reacquireVmAfterBlocking(released);
    }
    if (!loaded) {
        lua_pushnil(state);
        lua_pushstring(state, nativeError.c_str());
        return 2;
    }
    lua_pushboolean(state, 1);
    return 1;
}

/** 加载 APK 内置中文 PP-OCRv4 mobile 模型，名称默认 builtin，推理线程默认 2。 */
int luaOcrLoadBuiltin(lua_State* state) {
    std::string name = lua_isnoneornil(state, 1) ? "builtin" : luaL_checkstring(state, 1);
    int threads = lua_isnoneornil(state, 2) ? 2 : luaCheckInt(state, 2, "threads");

    // 首次调用需要复制模型并创建 ONNX session，耗时特征与自定义 load 相同；参数已复制到
    // native 局部变量，因此等待期间可以安全释放 VM Gate。
    lua_getfield(state, LUA_REGISTRYINDEX, "小鱼精灵Runtime");
    LuaRuntime* runtime = static_cast<LuaRuntime*>(lua_touserdata(state, -1));
    lua_pop(state, 1);
    bool released = runtime != nullptr && runtime->releaseVmForBlocking();
    bool loaded = engine_ocrLoadBuiltinModel(name.c_str(), threads) != 0;
    std::string nativeError = engine_ocrLastError();
    if (runtime != nullptr) {
        runtime->reacquireVmAfterBlocking(released);
    }
    if (!loaded) {
        lua_pushnil(state);
        lua_pushstring(state, nativeError.c_str());
        return 2;
    }
    lua_pushboolean(state, 1);
    return 1;
}

/** 释放一个脚本名称对 OCR 模型的持有。 */
int luaOcrRelease(lua_State* state) {
    const char* name = luaL_checkstring(state, 1);
    bool released = engine_ocrReleaseModel(name) != 0;
    if (!released && engine_ocrLastError()[0] != '\0') {
        lua_pushnil(state);
        lua_pushstring(state, engine_ocrLastError());
        return 2;
    }
    lua_pushboolean(state, released);
    return 1;
}

/** 查询 OCR 模型是否已加载。 */
int luaOcrIsLoaded(lua_State* state) {
    const char* name = luaL_checkstring(state, 1);
    bool loaded = engine_ocrIsModelLoaded(name) != 0;
    if (!loaded && engine_ocrLastError()[0] != '\0') {
        lua_pushnil(state);
        lua_pushstring(state, engine_ocrLastError());
        return 2;
    }
    lua_pushboolean(state, loaded);
    return 1;
}

/** 识别图片文件，返回每个文本框组成的 Lua table。 */
int luaOcrRead(lua_State* state) {
    std::string name = luaL_checkstring(state, 1);
    std::string path = luaL_checkstring(state, 2);
    std::string optionsJson;
    std::string error;
    if (!luaOptionalOptionsJson(state, 3, &optionsJson, &error)) {
        return luaL_argerror(state, 3, error.c_str());
    }
    lua_getfield(state, LUA_REGISTRYINDEX, "小鱼精灵Runtime");
    LuaRuntime* runtime = static_cast<LuaRuntime*>(lua_touserdata(state, -1));
    lua_pop(state, 1);
    bool released = runtime != nullptr && runtime->releaseVmForBlocking();
    const char* response = engine_ocrRead(name.c_str(), path.c_str(), optionsJson.c_str());
    std::string resultJson = response == nullptr ? "" : response;
    std::string nativeError = engine_ocrLastError();
    if (runtime != nullptr) {
        runtime->reacquireVmAfterBlocking(released);
    }
    return pushCAbiJsonOrError(state, resultJson.c_str(), nativeError.c_str());
}

/** 在图片 OCR 结果中查找文字。 */
int luaOcrFindText(lua_State* state) {
    std::string name = luaL_checkstring(state, 1);
    std::string path = luaL_checkstring(state, 2);
    std::string text = luaL_checkstring(state, 3);
    std::string optionsJson;
    std::string error;
    if (!luaOptionalOptionsJson(state, 4, &optionsJson, &error)) {
        return luaL_argerror(state, 4, error.c_str());
    }
    lua_getfield(state, LUA_REGISTRYINDEX, "小鱼精灵Runtime");
    LuaRuntime* runtime = static_cast<LuaRuntime*>(lua_touserdata(state, -1));
    lua_pop(state, 1);
    bool released = runtime != nullptr && runtime->releaseVmForBlocking();
    const char* response = engine_ocrFindText(name.c_str(), path.c_str(), text.c_str(), optionsJson.c_str());
    std::string resultJson = response == nullptr ? "" : response;
    std::string nativeError = engine_ocrLastError();
    if (runtime != nullptr) {
        runtime->reacquireVmAfterBlocking(released);
    }
    return pushCAbiJsonOrError(state, resultJson.c_str(), nativeError.c_str());
}

/** 替换指定索引的自定义点阵字库。 */
int luaFontSetDict(lua_State* state) {
    int index = luaCheckInt(state, 1, "index");
    const char* dictionary = luaL_checkstring(state, 2);
    if (!engine_fontSetDict(index, dictionary)) {
        lua_pushnil(state);
        lua_pushstring(state, engine_fontLastError());
        return 2;
    }
    lua_pushboolean(state, 1);
    return 1;
}

/** 向指定索引追加自定义点阵字库记录。 */
int luaFontAddDict(lua_State* state) {
    int index = luaCheckInt(state, 1, "index");
    const char* dictionary = luaL_checkstring(state, 2);
    if (!engine_fontAddDict(index, dictionary)) {
        lua_pushnil(state);
        lua_pushstring(state, engine_fontLastError());
        return 2;
    }
    lua_pushboolean(state, 1);
    return 1;
}

/** 为当前 Lua native 线程选择自定义点阵字库。 */
int luaFontUseDict(lua_State* state) {
    int index = luaCheckInt(state, 1, "index");
    if (!engine_fontUseDict(index)) {
        lua_pushnil(state);
        lua_pushstring(state, engine_fontLastError());
        return 2;
    }
    lua_pushboolean(state, 1);
    return 1;
}

/** 从当前截图生成可直接拼入新格式字库记录的字形点阵。 */
int luaFontGetPixel(lua_State* state) {
    int x1 = luaCheckInt(state, 1, "x1");
    int y1 = luaCheckInt(state, 2, "y1");
    int x2 = luaCheckInt(state, 3, "x2");
    int y2 = luaCheckInt(state, 4, "y2");
    const char* color = luaL_checkstring(state, 5);
    const char* result = engine_fontGetPixel(x1, y1, x2, y2, color);
    if (result == nullptr) {
        lua_pushnil(state);
        lua_pushstring(state, engine_fontLastError());
        return 2;
    }
    lua_pushstring(state, result);
    return 1;
}

/** 按当前点阵字库识字，返回 text/items Lua table。 */
int luaFontOcr(lua_State* state) {
    int x1 = luaCheckInt(state, 1, "x1");
    int y1 = luaCheckInt(state, 2, "y1");
    int x2 = luaCheckInt(state, 3, "x2");
    int y2 = luaCheckInt(state, 4, "y2");
    const char* color = luaL_checkstring(state, 5);
    double similarity = luaL_checknumber(state, 6);
    return pushCAbiJsonOrError(
            state,
            engine_fontOcr(x1, y1, x2, y2, color, similarity),
            engine_fontLastError()
    );
}

/** 按当前点阵字库查找一段文字。 */
int luaFontFindStr(lua_State* state) {
    int x1 = luaCheckInt(state, 1, "x1");
    int y1 = luaCheckInt(state, 2, "y1");
    int x2 = luaCheckInt(state, 3, "x2");
    int y2 = luaCheckInt(state, 4, "y2");
    const char* text = luaL_checkstring(state, 5);
    const char* color = luaL_checkstring(state, 6);
    double similarity = luaL_checknumber(state, 7);
    EnginePoint point{-1, -1};
    if (!engine_fontFindStr(x1, y1, x2, y2, text, color, similarity, &point)) {
        const char* error = engine_fontLastError();
        if (error == nullptr || error[0] == '\0') {
            lua_pushnil(state);
            return 1;
        }
        lua_pushnil(state);
        lua_pushstring(state, error);
        return 2;
    }
    lua_pushinteger(state, point.x);
    lua_pushinteger(state, point.y);
    return 2;
}

/** 按当前点阵字库返回全部文字命中坐标。 */
int luaFontFindStrEx(lua_State* state) {
    int x1 = luaCheckInt(state, 1, "x1");
    int y1 = luaCheckInt(state, 2, "y1");
    int x2 = luaCheckInt(state, 3, "x2");
    int y2 = luaCheckInt(state, 4, "y2");
    const char* text = luaL_checkstring(state, 5);
    const char* color = luaL_checkstring(state, 6);
    double similarity = luaL_checknumber(state, 7);
    return pushCAbiJsonOrError(
            state,
            engine_fontFindStrEx(x1, y1, x2, y2, text, color, similarity),
            engine_fontLastError()
    );
}

/**
 * 打开 dialog、hud 或 web UI 会话。
 *
 * Lua 层传入 table，HostApi 在这里转为 JSON 后交给统一 C ABI。这样未来 JS、Go
 * 绑定只需生成同一份 JSON，而不需要依赖 Lua userdata 或 Android View。
 */
int luaUiOpen(lua_State* state) {
    const char* surface = luaL_checkstring(state, 1);
    std::string specJson;
    std::string error;
    if (lua_gettop(state) < 2) {
        specJson = "{}";
    } else if (!luaArgumentToJson(state, 2, &specJson, &error)) {
        return luaL_error(state, "%s", error.c_str());
    }

    // 包内 HTML/图片资源不能通过普通 file:// 路径读取。运行时把当前包路径注入 web
    // 配置，Android 主进程据此从 ZIP 读取资源；普通 .lua 脚本不增加任何字段。
    if (surface != nullptr && std::string(surface) == "web") {
        lua_getfield(state, LUA_REGISTRYINDEX, "小鱼精灵Runtime");
        LuaRuntime* runtime = static_cast<LuaRuntime*>(lua_touserdata(state, -1));
        lua_pop(state, 1);
        std::string packagePath = runtime == nullptr ? "" : runtime->packagePath();
        if (!packagePath.empty()) {
            JsonValue spec;
            std::string parseError;
            if (!parseJsonText(specJson, &spec, &parseError) || !spec.isObject()) {
                return luaL_error(state, "HTML 配置 JSON 无效");
            }
            std::map<std::string, JsonValue> fields = spec.objectValue();
            fields["_alpkgPath"] = JsonValue::makeString(packagePath);
            specJson = jsonValueToString(JsonValue::makeObject(std::move(fields)));
        }
    }

    long long sessionId = engine_uiOpen(surface == nullptr ? "" : surface, specJson.c_str());
    if (sessionId <= 0) {
        lua_pushnil(state);
        lua_pushstring(state, engine_uiLastError());
        return 2;
    }

    lua_pushinteger(state, static_cast<lua_Integer>(sessionId));
    return 1;
}

/**
 * 更新 HUD 配置。
 */
int luaUiUpdate(lua_State* state) {
    lua_Integer sessionId = luaL_checkinteger(state, 1);
    std::string specJson;
    std::string error;
    if (!luaArgumentToJson(state, 2, &specJson, &error)) {
        return luaL_error(state, "%s", error.c_str());
    }

    if (!engine_uiUpdate(static_cast<long long>(sessionId), specJson.c_str())) {
        lua_pushnil(state);
        lua_pushstring(state, engine_uiLastError());
        return 2;
    }

    lua_pushboolean(state, 1);
    return 1;
}

/**
 * 向 HTML 页面发送 JSON 数据。
 */
int luaUiPostMessage(lua_State* state) {
    lua_Integer sessionId = luaL_checkinteger(state, 1);
    std::string messageJson;
    std::string error;
    if (!luaArgumentToJson(state, 2, &messageJson, &error)) {
        return luaL_error(state, "%s", error.c_str());
    }

    if (!engine_uiPostMessage(static_cast<long long>(sessionId), messageJson.c_str())) {
        lua_pushnil(state);
        lua_pushstring(state, engine_uiLastError());
        return 2;
    }

    lua_pushboolean(state, 1);
    return 1;
}

/**
 * 关闭 UI 会话。
 */
int luaUiClose(lua_State* state) {
    lua_Integer sessionId = luaL_checkinteger(state, 1);
    if (!engine_uiClose(static_cast<long long>(sessionId))) {
        lua_pushnil(state);
        lua_pushstring(state, engine_uiLastError());
        return 2;
    }

    lua_pushboolean(state, 1);
    return 1;
}

/**
 * 等待 UI 事件并转换为 Lua table。
 */
int luaUiWaitEvent(lua_State* state) {
    lua_Integer sessionId = luaL_checkinteger(state, 1);
    // Lua 包装函数可能把省略的可选参数继续传成 nil。none 和 nil 都表示无限等待，
    // 只有脚本真正提供数值时才执行整数校验。
    lua_Integer timeoutMs = lua_isnoneornil(state, 2) ? -1 : luaL_checkinteger(state, 2);
    if (timeoutMs < -1 || timeoutMs > std::numeric_limits<int>::max()) {
        return luaL_error(state, "UI 等待时间必须在 -1 到 int 最大值之间");
    }

    lua_getfield(state, LUA_REGISTRYINDEX, "小鱼精灵Runtime");
    LuaRuntime* runtime = static_cast<LuaRuntime*>(lua_touserdata(state, -1));
    lua_pop(state, 1);

    // UI 会话 ID 和超时已经复制完成，等待期间不触碰 Lua 栈，因此可以让出 VM Gate。
    bool released = runtime != nullptr && runtime->releaseVmForBlocking();
    const char* eventText = engine_uiWaitEventInterruptible(
            static_cast<long long>(sessionId),
            static_cast<int>(timeoutMs),
            luaShouldInterrupt,
            runtime
    );
    if (runtime != nullptr) {
        runtime->reacquireVmAfterBlocking(released);
    }
    if (eventText == nullptr || eventText[0] == '\0') {
        std::string error = engine_uiLastError();
        if (error == "脚本已停止") {
            engine_uiClose(static_cast<long long>(sessionId));
            return luaL_error(state, "%s", error.c_str());
        }
        lua_pushnil(state);
        lua_pushstring(state, error.empty() ? "UI 等待失败" : error.c_str());
        return 2;
    }

    JsonValue event;
    std::string parseError;
    if (!parseJsonText(eventText, &event, &parseError)) {
        lua_pushnil(state);
        lua_pushstring(state, "UI 事件 JSON 无效");
        return 2;
    }

    pushJsonValueToLua(state, event, 0);
    return 1;
}

/**
 * 关闭当前脚本所有 UI 会话。
 */
int luaUiCloseAll(lua_State* state) {
    engine_uiCloseAll();
    lua_pushboolean(state, 1);
    return 1;
}

} // namespace

void registerHostApi(lua_State* state) {
    lua_newtable(state);
    int hostTableIndex = lua_gettop(state);

    setFunctionField(state, hostTableIndex, "print", luaPrint);
    setFunctionField(state, hostTableIndex, "sleep", luaSleep);
    setFunctionField(state, hostTableIndex, "systemTime", luaSystemTime);
    setFunctionField(state, hostTableIndex, "tickCount", luaTickCount);
    setFunctionField(state, hostTableIndex, "read_alpkg_file", luaReadAlpkgFile);
    setFunctionField(state, hostTableIndex, "touchDown", luaTouchDown);
    setFunctionField(state, hostTableIndex, "touchMove", luaTouchMove);
    setFunctionField(state, hostTableIndex, "touchUp", luaTouchUp);
    setFunctionField(state, hostTableIndex, "keyDown", luaKeyDown);
    setFunctionField(state, hostTableIndex, "keyUp", luaKeyUp);
    setFunctionField(state, hostTableIndex, "keyPress", luaKeyPress);
    setFunctionField(state, hostTableIndex, "inputText", luaInputText);
    setFunctionField(state, hostTableIndex, "getRunEnvType", luaGetRunEnvType);

    // 设备、应用和系统控制固定绑定。它们都通过 system_c_api 调用 libengine.so/core/api，
    // Lua 这里仅负责参数和返回值转换，不直接触碰 Android Java API。
    setFunctionField(state, hostTableIndex, "appIsFront", luaAppIsFront);
    setFunctionField(state, hostTableIndex, "appIsRunning", luaAppIsRunning);
    setFunctionField(state, hostTableIndex, "frontAppName", luaFrontAppName);
    setFunctionField(state, hostTableIndex, "getCurrentActivity", luaGetCurrentActivity);
    setFunctionField(state, hostTableIndex, "runApp", luaRunApp);
    setFunctionField(state, hostTableIndex, "stopApp", luaStopApp);
    setFunctionField(state, hostTableIndex, "runIntent", luaRunIntent);
    setFunctionField(state, hostTableIndex, "installApk", luaInstallApk);
    setFunctionField(state, hostTableIndex, "getInstalledApk", luaGetInstalledApk);
    setFunctionField(state, hostTableIndex, "getInstalledApps", luaGetInstalledApps);
    setFunctionField(state, hostTableIndex, "getInsallAppInfos", luaGetInsallAppInfos);
    setFunctionField(state, hostTableIndex, "getApkVerInt", luaGetApkVerInt);
    setFunctionField(state, hostTableIndex, "exec", luaExec);
    setFunctionField(state, hostTableIndex, "exitScript", luaExitScript);
    setFunctionField(state, hostTableIndex, "getXiaoyvApi", luaGetXiaoyvApi);
    setFunctionField(state, hostTableIndex, "getBatteryLevel", luaGetBatteryLevel);
    setFunctionField(state, hostTableIndex, "getBoard", luaGetBoard);
    setFunctionField(state, hostTableIndex, "getBootLoader", luaGetBootLoader);
    setFunctionField(state, hostTableIndex, "getBrand", luaGetBrand);
    setFunctionField(state, hostTableIndex, "getCpuAbi", luaGetCpuAbi);
    setFunctionField(state, hostTableIndex, "getCpuAbi2", luaGetCpuAbi2);
    setFunctionField(state, hostTableIndex, "getCpuArch", luaGetCpuArch);
    setFunctionField(state, hostTableIndex, "getDevice", luaGetDevice);
    setFunctionField(state, hostTableIndex, "getDeviceId", luaGetDeviceId);
    setFunctionField(state, hostTableIndex, "getDisplayDpi", luaGetDisplayDpi);
    setFunctionField(state, hostTableIndex, "getDisplayInfo", luaGetDisplayInfo);
    setFunctionField(state, hostTableIndex, "getDisplayRotate", luaGetDisplayRotate);
    setFunctionField(state, hostTableIndex, "getDisplaySize", luaGetDisplaySize);
    setFunctionField(state, hostTableIndex, "getFingerprint", luaGetFingerprint);
    setFunctionField(state, hostTableIndex, "getHardware", luaGetHardware);
    setFunctionField(state, hostTableIndex, "getId", luaGetId);
    setFunctionField(state, hostTableIndex, "getManufacturer", luaGetManufacturer);
    setFunctionField(state, hostTableIndex, "getModel", luaGetModel);
    setFunctionField(state, hostTableIndex, "getNetWorkTime", luaGetNetWorkTime);
    setFunctionField(state, hostTableIndex, "getOaid", luaGetOaid);
    setFunctionField(state, hostTableIndex, "getOsVersionName", luaGetOsVersionName);
    setFunctionField(state, hostTableIndex, "getPackageName", luaGetPackageName);
    setFunctionField(state, hostTableIndex, "getProduct", luaGetProduct);
    setFunctionField(state, hostTableIndex, "getSdPath", luaGetSdPath);
    setFunctionField(state, hostTableIndex, "getSdkVersion", luaGetSdkVersion);
    setFunctionField(state, hostTableIndex, "getSensorsInfo", luaGetSensorsInfo);
    setFunctionField(state, hostTableIndex, "getSimSerialNumber", luaGetSimSerialNumber);
    setFunctionField(state, hostTableIndex, "getSubscriberId", luaGetSubscriberId);
    setFunctionField(state, hostTableIndex, "getWifiMac", luaGetWifiMac);
    setFunctionField(state, hostTableIndex, "getWorkPath", luaGetWorkPath);
    setFunctionField(state, hostTableIndex, "lockScreen", luaLockScreen);
    setFunctionField(state, hostTableIndex, "unLockScreen", luaUnLockScreen);
    setFunctionField(state, hostTableIndex, "setDisplayPowerOff", luaSetDisplayPowerOff);
    setFunctionField(state, hostTableIndex, "setAirplaneMode", luaSetAirplaneMode);
    setFunctionField(state, hostTableIndex, "setBTEnable", luaSetBTEnable);
    setFunctionField(state, hostTableIndex, "setWifiEnable", luaSetWifiEnable);
    setFunctionField(state, hostTableIndex, "phoneCall", luaPhoneCall);
    setFunctionField(state, hostTableIndex, "sendSms", luaSendSms);
    setFunctionField(state, hostTableIndex, "vibrate", luaVibrate);

    // Lua 多线程属于语言运行时能力，直接实现于 libengine.so/runtime/lua，不经过
    // 语言无关 C ABI；JS 和 Go 后续分别使用自己的任务模型。
    registerLuaThreadApi(state, hostTableIndex);

    lua_newtable(state);
    int logTableIndex = lua_gettop(state);
    setFunctionField(state, logTableIndex, "print", luaLogPrint);
    lua_setfield(state, hostTableIndex, "log");

    lua_newtable(state);
    int screenTableIndex = lua_gettop(state);
    setFunctionField(state, screenTableIndex, "getScreenPixels", luaGetScreenPixels);
    setFunctionField(state, screenTableIndex, "setScreenPixels", luaSetScreenPixels);
    setFunctionField(state, screenTableIndex, "restoreScreenPixels", luaRestoreScreenPixels);
    setFunctionField(state, screenTableIndex, "keepCapture", luaKeepCapture);
    setFunctionField(state, screenTableIndex, "releaseCapture", luaReleaseCapture);
    setFunctionField(state, screenTableIndex, "setCaptureCacheMs", luaSetCaptureCacheMs);
    setFunctionField(state, screenTableIndex, "capture", luaCapture);
    lua_setfield(state, hostTableIndex, "screen");

    lua_newtable(state);
    int colorTableIndex = lua_gettop(state);
    setFunctionField(state, colorTableIndex, "findColors", luaColorsFind);
    lua_setfield(state, hostTableIndex, "color");

    lua_newtable(state);
    int imageTableIndex = lua_gettop(state);
    setFunctionField(state, imageTableIndex, "findPic", luaFindPic);
    setFunctionField(state, imageTableIndex, "clearCache", luaClearImageCache);
    setFunctionField(state, imageTableIndex, "setCacheMaxBytes", luaSetImageCacheMaxBytes);
    lua_setfield(state, hostTableIndex, "image");

    lua_newtable(state);
    int ocrTableIndex = lua_gettop(state);
    setFunctionField(state, ocrTableIndex, "load", luaOcrLoad);
    setFunctionField(state, ocrTableIndex, "loadBuiltin", luaOcrLoadBuiltin);
    setFunctionField(state, ocrTableIndex, "release", luaOcrRelease);
    setFunctionField(state, ocrTableIndex, "isLoaded", luaOcrIsLoaded);
    setFunctionField(state, ocrTableIndex, "read", luaOcrRead);
    setFunctionField(state, ocrTableIndex, "findText", luaOcrFindText);
    lua_setfield(state, hostTableIndex, "ocr");

    lua_newtable(state);
    int fontTableIndex = lua_gettop(state);
    setFunctionField(state, fontTableIndex, "setDict", luaFontSetDict);
    setFunctionField(state, fontTableIndex, "addDict", luaFontAddDict);
    setFunctionField(state, fontTableIndex, "useDict", luaFontUseDict);
    setFunctionField(state, fontTableIndex, "getFontPixel", luaFontGetPixel);
    setFunctionField(state, fontTableIndex, "ocr", luaFontOcr);
    setFunctionField(state, fontTableIndex, "findStr", luaFontFindStr);
    setFunctionField(state, fontTableIndex, "findStrEx", luaFontFindStrEx);
    lua_setfield(state, hostTableIndex, "font");

    lua_newtable(state);
    int imeTableIndex = lua_gettop(state);
    setFunctionField(state, imeTableIndex, "lock", luaImeLock);
    setFunctionField(state, imeTableIndex, "setText", luaImeSetText);
    setFunctionField(state, imeTableIndex, "unlock", luaImeUnlock);
    lua_setfield(state, hostTableIndex, "ime");

    lua_newtable(state);
    int uiTableIndex = lua_gettop(state);
    setFunctionField(state, uiTableIndex, "open", luaUiOpen);
    setFunctionField(state, uiTableIndex, "update", luaUiUpdate);
    setFunctionField(state, uiTableIndex, "postMessage", luaUiPostMessage);
    setFunctionField(state, uiTableIndex, "close", luaUiClose);
    setFunctionField(state, uiTableIndex, "waitEvent", luaUiWaitEvent);
    setFunctionField(state, uiTableIndex, "closeAll", luaUiCloseAll);
    lua_setfield(state, hostTableIndex, "ui");

    lua_setglobal(state, "_host");
}
