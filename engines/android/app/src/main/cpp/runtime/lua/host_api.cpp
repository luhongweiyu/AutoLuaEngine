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
    lua_pushstring(state, engine_getRunEnvType());
    return 1;
}

/**
 * Lua 截图入口。
 *
 * 成功返回：width, height, pixelsAddress。
 * 失败返回：nil, errorMessage。
 */
int luaRootCapture(lua_State* state) {
    int width = 0;
    int height = 0;
    unsigned char* pixels = nullptr;

    if (!engine_capture(&width, &height, &pixels)) {
        lua_pushnil(state);
        lua_pushstring(state, engine_captureLastError());
        return 2;
    }

    lua_pushinteger(state, static_cast<lua_Integer>(width));
    lua_pushinteger(state, static_cast<lua_Integer>(height));
    lua_pushinteger(state, static_cast<lua_Integer>(reinterpret_cast<std::uintptr_t>(pixels)));
    return 3;
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
        lua_pushstring(state, engine_captureLastError());
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

    // Lua 多线程属于语言运行时能力，直接实现于 libengine.so/runtime/lua，不经过
    // 语言无关 C ABI；JS 和 Go 后续分别使用自己的任务模型。
    registerLuaThreadApi(state, hostTableIndex);

    lua_newtable(state);
    int logTableIndex = lua_gettop(state);
    setFunctionField(state, logTableIndex, "print", luaLogPrint);
    lua_setfield(state, hostTableIndex, "log");

    lua_newtable(state);
    int screenTableIndex = lua_gettop(state);
    setFunctionField(state, screenTableIndex, "capture", luaRootCapture);
    setFunctionField(state, screenTableIndex, "keepCapture", luaKeepCapture);
    setFunctionField(state, screenTableIndex, "releaseCapture", luaReleaseCapture);
    setFunctionField(state, screenTableIndex, "setCaptureCacheMs", luaSetCaptureCacheMs);
    lua_setfield(state, hostTableIndex, "screen");

    lua_newtable(state);
    int colorTableIndex = lua_gettop(state);
    setFunctionField(state, colorTableIndex, "findColors", luaColorsFind);
    lua_setfield(state, hostTableIndex, "color");

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
