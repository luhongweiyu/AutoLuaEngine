/**
 * 文件用途：注册 Lua 最小 HostApi，把脚本侧 m.capture 等函数连接到 libengine.so 的 screen_* C ABI。
 */
#include "host_api.h"

#include <algorithm>
#include <android/log.h>
#include <chrono>
#include <cstdint>
#include <sstream>
#include <string>
#include <thread>

#include "../../core/system_c_api.h"
#include "../common/log_buffer.h"
#include "lua_runtime.h"

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
 * 把 Lua 任意值转成字符串，保持 print(...) 和 Lua 原生 __tostring 行为一致。
 */
std::string luaValueToString(lua_State* state, int index) {
    luaL_tolstring(state, index, nullptr);
    const char* text = lua_tostring(state, -1);
    std::string result = text == nullptr ? "" : text;
    lua_pop(state, 1);
    return result;
}

void setFunctionField(lua_State* state, int tableIndex, const char* name, lua_CFunction function) {
    lua_pushcfunction(state, function);
    lua_setfield(state, tableIndex, name);
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

    lua_getfield(state, LUA_REGISTRYINDEX, "AutoLuaEngineRuntime");
    LuaRuntime* runtime = static_cast<LuaRuntime*>(lua_touserdata(state, -1));
    lua_pop(state, 1);

    const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(duration);
    while (std::chrono::steady_clock::now() < deadline) {
        if (runtime != nullptr && runtime->shouldInterruptNow()) {
            return luaL_error(state, "script stopped");
        }

        auto remaining = deadline - std::chrono::steady_clock::now();
        auto slice = std::min(
                std::chrono::duration_cast<std::chrono::milliseconds>(remaining),
                std::chrono::milliseconds(50)
        );
        if (slice.count() > 0) {
            std::this_thread::sleep_for(slice);
        }
    }

    lua_pushboolean(state, 1);
    return 1;
}

int luaLogPrint(lua_State* state) {
    const char* text = luaL_checkstring(state, 1);
    logInfo(text == nullptr ? "" : text);
    lua_pushboolean(state, 1);
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

    if (!screen_capture(&width, &height, &pixels)) {
        lua_pushnil(state);
        lua_pushstring(state, screen_last_error());
        return 2;
    }

    lua_pushinteger(state, static_cast<lua_Integer>(width));
    lua_pushinteger(state, static_cast<lua_Integer>(height));
    lua_pushinteger(state, static_cast<lua_Integer>(reinterpret_cast<std::uintptr_t>(pixels)));
    return 3;
}

int luaKeepCapture(lua_State* state) {
    screen_keep_capture();
    lua_pushboolean(state, 1);
    return 1;
}

int luaReleaseCapture(lua_State* state) {
    screen_release_capture();
    lua_pushboolean(state, 1);
    return 1;
}

int luaSetCaptureCacheMs(lua_State* state) {
    lua_Integer durationMs = luaL_checkinteger(state, 1);
    if (!screen_set_capture_cache_ms(static_cast<int>(durationMs))) {
        lua_pushnil(state);
        lua_pushstring(state, screen_last_error());
        return 2;
    }

    lua_pushinteger(state, durationMs);
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
    int screenTableIndex = lua_gettop(state);
    setFunctionField(state, screenTableIndex, "capture", luaRootCapture);
    setFunctionField(state, screenTableIndex, "keepCapture", luaKeepCapture);
    setFunctionField(state, screenTableIndex, "releaseCapture", luaReleaseCapture);
    setFunctionField(state, screenTableIndex, "setCaptureCacheMs", luaSetCaptureCacheMs);
    lua_setfield(state, hostTableIndex, "screen");

    lua_setglobal(state, "_host");
}
