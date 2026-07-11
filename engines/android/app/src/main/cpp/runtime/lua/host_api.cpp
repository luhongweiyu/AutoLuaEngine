/**
 * 文件用途：注册 Lua 最小 HostApi，只负责 Lua 参数转换并调用 libengine.so C ABI。
 */
#include "host_api.h"

#include <cstdint>
#include <limits>
#include <sstream>
#include <string>

#include "../../core/system_c_api.h"
#include "lua_runtime.h"

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
        luaL_error(state, "%s is out of int range", name);
        return 0;
    }
    return static_cast<int>(value);
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
        return luaL_error(state, "sleep duration must be greater than or equal to 0");
    }
    if (duration > std::numeric_limits<int>::max()) {
        return luaL_error(state, "sleep duration is too large");
    }

    lua_getfield(state, LUA_REGISTRYINDEX, "AutoLuaEngineRuntime");
    LuaRuntime* runtime = static_cast<LuaRuntime*>(lua_touserdata(state, -1));
    lua_pop(state, 1);

    if (!engine_sleepInterruptible(static_cast<int>(duration), luaShouldInterrupt, runtime)) {
        const char* error = engine_runtimeLastError();
        return luaL_error(state, error == nullptr || error[0] == '\0' ? "script stopped" : error);
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

} // namespace

void registerHostApi(lua_State* state) {
    lua_newtable(state);
    int hostTableIndex = lua_gettop(state);

    setFunctionField(state, hostTableIndex, "print", luaPrint);
    setFunctionField(state, hostTableIndex, "sleep", luaSleep);
    setFunctionField(state, hostTableIndex, "systemTime", luaSystemTime);
    setFunctionField(state, hostTableIndex, "tickCount", luaTickCount);

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

    lua_setglobal(state, "_host");
}
