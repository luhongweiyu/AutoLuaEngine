/**
 * 文件用途：实现懒人精灵兼容的 Lua imgui 表、参数转换和 VM Gate 事件回调。
 */
#include "imgui_lua_api.h"

#include <atomic>
#include <charconv>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "java_bridge.h"
#include "lua_runtime.h"
#include "../../core/imgui_c_api.h"
#include "../../core/system_c_api.h"
#include "imgui.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

namespace {

constexpr const char* kCallbackTableKey = "小鱼精灵ImGuiCallbacks";
constexpr const char* kPumpRunningKey = "小鱼精灵ImGui事件泵运行中";
std::atomic<long long> gNextPostId{1};

int shouldInterrupt(void* context) {
    auto* runtime = static_cast<LuaRuntime*>(context);
    return runtime != nullptr && runtime->shouldInterruptNow() ? 1 : 0;
}

EngineImGuiHandle checkHandle(lua_State* state, int index) {
    return static_cast<EngineImGuiHandle>(luaL_checkinteger(state, index));
}

float checkFloat(lua_State* state, int index) {
    return static_cast<float>(luaL_checknumber(state, index));
}

float optionalFloat(lua_State* state, int index, float defaultValue) {
    return lua_isnoneornil(state, index)
            ? defaultValue
            : static_cast<float>(luaL_checknumber(state, index));
}

int checkInt(lua_State* state, int index, const char* name) {
    lua_Integer value = luaL_checkinteger(state, index);
    if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) {
        luaL_error(state, "%s 超出整数范围", name);
    }
    return static_cast<int>(value);
}

std::uint32_t checkColor(lua_State* state, int index) {
    const lua_Integer value = luaL_checkinteger(state, index);
    if (value < static_cast<lua_Integer>(std::numeric_limits<std::int32_t>::min())
            || value > static_cast<lua_Integer>(std::numeric_limits<std::uint32_t>::max())) {
        luaL_argerror(state, index, "颜色必须是 32 位 0xAARRGGBB 整数");
    }
    return static_cast<std::uint32_t>(value);
}

std::string callbackKey(const char* event, long long handle) {
    return std::string(event) + ":" + std::to_string(handle);
}

void pushCallbackTable(lua_State* state) {
    lua_getfield(state, LUA_REGISTRYINDEX, kCallbackTableKey);
    if (lua_istable(state, -1)) {
        return;
    }
    lua_pop(state, 1);
    lua_newtable(state);
    lua_pushvalue(state, -1);
    lua_setfield(state, LUA_REGISTRYINDEX, kCallbackTableKey);
}

/** 保存或删除一个 Lua 回调；渲染线程只知道 event+handle，不持有 lua_State。 */
void setCallback(lua_State* state, const char* event, EngineImGuiHandle handle, int index) {
    if (!lua_isfunction(state, index) && !lua_isnil(state, index)) {
        luaL_argerror(state, index, "回调必须是函数或 nil");
    }
    if (lua_isfunction(state, index)
            && std::string(event) != "post"
            && !engine_imguiIsValidHandle(handle)) {
        luaL_argerror(state, 1, "ImGui 句柄无效");
    }
    pushCallbackTable(state);
    lua_pushvalue(state, index);
    lua_setfield(state, -2, callbackKey(event, handle).c_str());
    lua_pop(state, 1);
}

/**
 * 清理已经随窗口子树销毁的 Lua 回调。
 *
 * C++ 模型会递归销毁窗口中的全部子控件，但语言回调有意不进入 C ABI。这里遍历 registry
 * 并删除所有失效控件句柄对应的回调，避免长时间脚本反复创建/销毁窗口时积累函数引用。
 * post 使用独立序号且可能仍在事件队列中，因此不会参与句柄有效性清理。
 */
void pruneInvalidCallbacks(lua_State* state) {
    pushCallbackTable(state);
    const int table = lua_gettop(state);
    lua_pushnil(state);
    while (lua_next(state, table) != 0) {
        bool remove = false;
        if (lua_type(state, -2) == LUA_TSTRING) {
            std::size_t keyLength = 0;
            const char* key = lua_tolstring(state, -2, &keyLength);
            const std::string keyText = key == nullptr ? std::string() : std::string(key, keyLength);
            const std::size_t separator = keyText.rfind(':');
            if (separator != std::string::npos && keyText.substr(0, separator) != "post") {
                long long handle = 0;
                const char* begin = keyText.data() + separator + 1;
                const char* end = keyText.data() + keyText.size();
                const auto parsed = std::from_chars(begin, end, handle);
                remove = parsed.ec == std::errc{} && parsed.ptr == end
                        && !engine_imguiIsValidHandle(handle);
            }
        }

        lua_pop(state, 1);
        if (remove) {
            lua_pushvalue(state, -1);
            lua_pushnil(state);
            lua_rawset(state, table);
        }
    }
    lua_pop(state, 1);
}

void logCallbackError(lua_State* state) {
    const char* error = lua_tostring(state, -1);
    std::string message = "ImGui 回调执行失败：";
    message += error == nullptr ? "未知错误" : error;
    engine_print(message.c_str());
    lua_pop(state, 1);
}

/**
 * 从 registry 取得并执行一条事件对应的回调。
 *
 * 返回值只对 WindowClose 有意义：回调明确返回 false 时阻止关闭，其他情况允许关闭。
 */
bool dispatchEvent(lua_State* state, const EngineImGuiEvent& event) {
    const char* eventName = nullptr;
    int argumentCount = 0;
    switch (event.type) {
        case ENGINE_IMGUI_EVENT_CLICK: eventName = "click"; argumentCount = 1; break;
        case ENGINE_IMGUI_EVENT_CHECK: eventName = "check"; argumentCount = 2; break;
        case ENGINE_IMGUI_EVENT_SELECT: eventName = "select"; argumentCount = 3; break;
        case ENGINE_IMGUI_EVENT_TABLE_SELECT: eventName = "table"; argumentCount = 4; break;
        case ENGINE_IMGUI_EVENT_SLIDER: eventName = "slider"; argumentCount = 2; break;
        case ENGINE_IMGUI_EVENT_WINDOW_CLOSE: eventName = "close"; argumentCount = 1; break;
        case ENGINE_IMGUI_EVENT_POST: eventName = "post"; argumentCount = 0; break;
        default: return true;
    }

    pushCallbackTable(state);
    int tableIndex = lua_gettop(state);
    std::string key = callbackKey(eventName, event.handle);
    lua_getfield(state, tableIndex, key.c_str());
    if (!lua_isfunction(state, -1)) {
        lua_pop(state, 2);
        return true;
    }
    if (event.type == ENGINE_IMGUI_EVENT_POST) {
        lua_pushnil(state);
        lua_setfield(state, tableIndex, key.c_str());
    }
    lua_remove(state, tableIndex);

    if (argumentCount >= 1) lua_pushinteger(state, event.handle);
    if (event.type == ENGINE_IMGUI_EVENT_CHECK) {
        lua_pushboolean(state, event.boolValue != 0);
    } else if (event.type == ENGINE_IMGUI_EVENT_SELECT) {
        lua_pushinteger(state, event.index);
        lua_pushstring(state, event.text == nullptr ? "" : event.text);
    } else if (event.type == ENGINE_IMGUI_EVENT_TABLE_SELECT) {
        lua_pushinteger(state, event.row);
        lua_pushinteger(state, event.column);
        lua_pushstring(state, event.text == nullptr ? "" : event.text);
    } else if (event.type == ENGINE_IMGUI_EVENT_SLIDER) {
        lua_pushinteger(state, event.integerValue);
    }

    const int resultCount = event.type == ENGINE_IMGUI_EVENT_WINDOW_CLOSE ? 1 : 0;
    if (lua_pcall(state, argumentCount, resultCount, 0) != LUA_OK) {
        logCallbackError(state);
        return true;
    }
    if (resultCount == 0) {
        return true;
    }
    bool allowClose = !lua_isboolean(state, -1) || lua_toboolean(state, -1) != 0;
    lua_pop(state, 1);
    return allowClose;
}

/**
 * 内部 native 子任务事件泵。
 *
 * 等待期间释放 VM Gate；取得事件后重新持有 Gate 再调用 Lua，因此渲染线程和 Android
 * UI 线程永远不会直接执行脚本函数。
 */
int luaImGuiEventPump(lua_State* state) {
    LuaRuntime* runtime = LuaRuntime::fromState(state);
    while (runtime != nullptr && !runtime->shouldInterruptNow(state)) {
        EngineImGuiEvent event{};
        bool released = runtime->releaseVmForBlocking();
        int received = engine_imguiWaitEvent(&event, -1, shouldInterrupt, runtime);
        runtime->reacquireVmAfterBlocking(released);
        if (!received) {
            std::string error = engine_imguiLastError();
            if (error == "脚本已停止") break;
            if (!error.empty()) {
                std::string message = "ImGui 事件泵失败：" + error;
                engine_print(message.c_str());
                break;
            }
            continue;
        }
        if (event.type == ENGINE_IMGUI_EVENT_FRAMEWORK_CLOSED) {
            // 同一脚本可以 close 后再次 show；事件泵保持存活，直到整个脚本任务停止。
            continue;
        }
        bool allowClose = dispatchEvent(state, event);
        if (event.type == ENGINE_IMGUI_EVENT_WINDOW_CLOSE) {
            if (engine_imguiResolveWindowClose(event.handle, allowClose ? 1 : 0) && allowClose) {
                pruneInvalidCallbacks(state);
            }
        }
    }

    lua_pushboolean(state, 0);
    lua_setfield(state, LUA_REGISTRYINDEX, kPumpRunningKey);
    return 0;
}

/** 只为一个 LuaRuntime 创建一个事件泵，重复 show/showWindow 不增加线程。 */
bool ensureEventPump(lua_State* state, std::string* error) {
    lua_getfield(state, LUA_REGISTRYINDEX, kPumpRunningKey);
    bool running = lua_toboolean(state, -1) != 0;
    lua_pop(state, 1);
    if (running) {
        return true;
    }

    LuaRuntime* runtime = LuaRuntime::fromState(state);
    if (runtime == nullptr) {
        if (error != nullptr) *error = "LuaRuntime 不可用";
        return false;
    }
    lua_pushboolean(state, 1);
    lua_setfield(state, LUA_REGISTRYINDEX, kPumpRunningKey);
    lua_pushcfunction(state, luaImGuiEventPump);
    int functionIndex = lua_gettop(state);
    // 事件泵属于引擎基础设施，不占用脚本可创建的 10 个用户子线程名额。
    long long taskId = runtime->startInternalChildThread(state, functionIndex, 0, error);
    lua_pop(state, 1);
    if (taskId <= 0) {
        lua_pushboolean(state, 0);
        lua_setfield(state, LUA_REGISTRYINDEX, kPumpRunningKey);
        return false;
    }
    return true;
}

int luaIsSupport(lua_State* state) {
    lua_pushboolean(state, engine_imguiIsSupport());
    return 1;
}

int luaGetLastError(lua_State* state) {
    lua_pushstring(state, engine_imguiLastError());
    return 1;
}

int luaClose(lua_State* state) {
    (void) state;
    engine_imguiClose();
    return 0;
}

int luaSetColorTheme(lua_State* state) {
    lua_pushboolean(state, engine_imguiSetColorTheme(checkInt(state, 1, "style")));
    return 1;
}

/** 按类型而非位置解析 show 的三个可选参数，兼容懒人精灵文档。 */
int luaShow(lua_State* state) {
    EngineImGuiSurfaceConfig config{};
    config.windowed = 0;
    config.touchable = 1;
    config.width = -1;
    config.height = -1;
    config.titleFontSize = 100.0F;
    config.fontPath = "";
    config.fontSize = 45.0F;

    bool hasBoolean = false;
    bool hasString = false;
    bool hasNumber = false;
    std::string fontPath;
    for (int index = 1; index <= lua_gettop(state); ++index) {
        int type = lua_type(state, index);
        if (type == LUA_TNIL) continue;
        if (type == LUA_TBOOLEAN && !hasBoolean) {
            hasBoolean = true;
            config.touchable = lua_toboolean(state, index) ? 1 : 0;
        } else if (type == LUA_TSTRING && !hasString) {
            hasString = true;
            fontPath = lua_tostring(state, index);
        } else if (type == LUA_TNUMBER && !hasNumber) {
            hasNumber = true;
            config.fontSize = checkFloat(state, index);
        } else {
            return luaL_error(state, "imgui.show 参数类型重复或不受支持");
        }
    }
    config.fontPath = fontPath.c_str();
    if (!engine_imguiShow(&config)) {
        lua_pushboolean(state, 0);
        return 1;
    }
    std::string pumpError;
    if (!ensureEventPump(state, &pumpError)) {
        engine_imguiClose();
        return luaL_error(state, "%s", pumpError.c_str());
    }

    if (config.touchable) {
        LuaRuntime* runtime = LuaRuntime::fromState(state);
        bool released = runtime != nullptr && runtime->releaseVmForBlocking();
        int closed = engine_imguiWaitClosed(shouldInterrupt, runtime);
        if (runtime != nullptr) runtime->reacquireVmAfterBlocking(released);
        if (!closed) {
            if (std::string(engine_imguiLastError()) == "脚本已停止") {
                return luaL_error(state, "脚本已停止");
            }
            lua_pushboolean(state, 0);
            return 1;
        }
    }
    lua_pushboolean(state, 1);
    return 1;
}

template<typename T>
T tableField(lua_State* state, int table, const char* name, T defaultValue);

template<>
int tableField<int>(lua_State* state, int table, const char* name, int defaultValue) {
    lua_getfield(state, table, name);
    int value = lua_isnil(state, -1) ? defaultValue : checkInt(state, -1, name);
    lua_pop(state, 1);
    return value;
}

template<>
float tableField<float>(lua_State* state, int table, const char* name, float defaultValue) {
    lua_getfield(state, table, name);
    float value = lua_isnil(state, -1) ? defaultValue : checkFloat(state, -1);
    lua_pop(state, 1);
    return value;
}

template<>
bool tableField<bool>(lua_State* state, int table, const char* name, bool defaultValue) {
    lua_getfield(state, table, name);
    bool value = lua_isnil(state, -1) ? defaultValue : lua_toboolean(state, -1) != 0;
    lua_pop(state, 1);
    return value;
}

std::string stringField(lua_State* state, int table, const char* name, const char* defaultValue) {
    lua_getfield(state, table, name);
    std::string value = lua_isnil(state, -1)
            ? defaultValue
            : luaL_checkstring(state, -1);
    lua_pop(state, 1);
    return value;
}

/**
 * 读取 showWindow 的 32 位颜色字段。
 *
 * Lua 5.4 整数是 64 位，0xFFFFFFFF 会超过 C++ 有符号 int；颜色不能借用普通整数
 * 字段解析，否则脚本显式传入高位带 Alpha 的颜色会被错误拒绝。
 */
std::uint32_t colorField(
        lua_State* state,
        int table,
        const char* name,
        std::uint32_t defaultValue
) {
    lua_getfield(state, table, name);
    const std::uint32_t value = lua_isnil(state, -1)
            ? defaultValue
            : checkColor(state, -1);
    lua_pop(state, 1);
    return value;
}

/** 读取 showWindow 配置并立即启动内部事件泵。 */
int luaShowWindow(lua_State* state) {
    luaL_checktype(state, 1, LUA_TTABLE);
    int table = lua_absindex(state, 1);
    std::string title = stringField(state, table, "title", "");
    std::string font = stringField(state, table, "font", "");
    EngineImGuiSurfaceConfig config{};
    config.windowed = 1;
    config.touchable = 1;
    config.x = tableField<int>(state, table, "x", 0);
    config.y = tableField<int>(state, table, "y", 0);
    config.width = tableField<int>(state, table, "width", 600);
    config.height = tableField<int>(state, table, "height", 600);
    config.hasTitle = tableField<bool>(state, table, "hastitle", true) ? 1 : 0;
    config.title = title.c_str();
    config.titleColor = colorField(state, table, "titlecolor", 0xFFFFFFFFU);
    config.titleBackgroundColor = colorField(state, table, "titlebgcolor", 0xFF87CEFAU);
    config.hasClose = tableField<bool>(state, table, "hasclose", true) ? 1 : 0;
    config.closeColor = colorField(state, table, "closecolor", 0xFFFFFFFFU);
    config.hasResize = tableField<bool>(state, table, "hasresize", true) ? 1 : 0;
    config.resizeColor = colorField(state, table, "resizecolor", 0xFFFFFFFFU);
    config.hasToggle = tableField<bool>(state, table, "hastoggle", true) ? 1 : 0;
    config.toggleColor = colorField(state, table, "togglecolor", 0xFFFFFFFFU);
    config.titleFontSize = tableField<float>(state, table, "fontsize", 100.0F);
    config.fontPath = font.c_str();
    config.fontSize = tableField<float>(state, table, "contentfontsize", 45.0F);

    if (!engine_imguiShow(&config)) {
        lua_pushboolean(state, 0);
        return 1;
    }
    std::string error;
    if (!ensureEventPump(state, &error)) {
        engine_imguiClose();
        return luaL_error(state, "%s", error.c_str());
    }
    lua_pushboolean(state, 1);
    return 1;
}

int pushHandle(lua_State* state, EngineImGuiHandle handle, bool nilOnFailure) {
    if (handle > 0) {
        lua_pushinteger(state, handle);
    } else if (nilOnFailure) {
        lua_pushnil(state);
    } else {
        lua_pushinteger(state, 0);
    }
    return 1;
}

/** 窗口与布局绑定。 */
int luaCreateWindow(lua_State* state) {
    return pushHandle(state, engine_imguiCreateWindow(
            luaL_checkstring(state, 1),
            checkFloat(state, 2),
            checkFloat(state, 3),
            checkFloat(state, 4),
            checkFloat(state, 5),
            lua_toboolean(state, 6)
    ), false);
}

int luaDestroyWindow(lua_State* state) {
    if (engine_imguiDestroyWindow(checkHandle(state, 1))) {
        pruneInvalidCallbacks(state);
    }
    return 0;
}

int luaCreateVerticalLayout(lua_State* state) {
    return pushHandle(state, engine_imguiCreateVerticalLayout(
            checkHandle(state, 1),
            optionalFloat(state, 2, 0.0F),
            optionalFloat(state, 3, 0.0F)
    ), false);
}

int luaCreateHorticalLayout(lua_State* state) {
    return pushHandle(state, engine_imguiCreateHorticalLayout(
            checkHandle(state, 1),
            optionalFloat(state, 2, 0.0F),
            optionalFloat(state, 3, 0.0F)
    ), false);
}

int luaCreateTreeBoxLayout(lua_State* state) {
    return pushHandle(state, engine_imguiCreateTreeBoxLayout(
            checkHandle(state, 1),
            luaL_checkstring(state, 2),
            checkFloat(state, 3)
    ), true);
}

int luaCreateTabBar(lua_State* state) {
    return pushHandle(state, engine_imguiCreateTabBar(
            checkHandle(state, 1),
            luaL_checkstring(state, 2)
    ), true);
}

int luaAddTabBarItem(lua_State* state) {
    return pushHandle(state, engine_imguiAddTabBarItem(
            checkHandle(state, 1),
            luaL_checkstring(state, 2)
    ), true);
}

int luaSameLine(lua_State* state) {
    lua_pushboolean(state, engine_imguiSameLine(
            checkHandle(state, 1),
            checkFloat(state, 2)
    ));
    return 1;
}

int luaSetLayoutBorderVisible(lua_State* state) {
    engine_imguiSetLayoutBorderVisible(checkHandle(state, 1), lua_toboolean(state, 2));
    return 0;
}

int luaSetWindowPos(lua_State* state) {
    engine_imguiSetWindowPos(checkHandle(state, 1), checkFloat(state, 2), checkFloat(state, 3));
    return 0;
}

int luaSetWindowSize(lua_State* state) {
    engine_imguiSetWindowSize(checkHandle(state, 1), checkFloat(state, 2), checkFloat(state, 3));
    return 0;
}

int luaGetWindowPos(lua_State* state) {
    EngineImGuiGeometry geometry{};
    if (!engine_imguiGetWindowPos(checkHandle(state, 1), &geometry)) {
        lua_pushnil(state);
        return 1;
    }
    lua_createtable(state, 4, 4);
    const float values[] = {geometry.x, geometry.y, geometry.width, geometry.height};
    const char* names[] = {"x", "y", "width", "height"};
    for (int index = 0; index < 4; ++index) {
        lua_pushnumber(state, values[index]);
        lua_rawseti(state, -2, index + 1);
        lua_pushnumber(state, values[index]);
        lua_setfield(state, -2, names[index]);
    }
    return 1;
}

int luaSetWindowFlags(lua_State* state) {
    engine_imguiSetWindowFlags(checkHandle(state, 1), checkInt(state, 2, "flags"));
    return 0;
}

/** 基础控件绑定；createButton 同时兼容四参数 parent 形式和五参数绝对坐标形式。 */
int luaCreateButton(lua_State* state) {
    EngineImGuiHandle handle;
    if (lua_gettop(state) == 4) {
        handle = engine_imguiCreateButton(
                checkHandle(state, 1),
                luaL_checkstring(state, 2),
                0.0F,
                0.0F,
                checkFloat(state, 3),
                checkFloat(state, 4)
        );
    } else {
        luaL_argcheck(state, lua_gettop(state) == 5, 1, "createButton 需要 4 或 5 个参数");
        handle = engine_imguiCreateButton(
                0,
                luaL_checkstring(state, 5),
                checkFloat(state, 1),
                checkFloat(state, 2),
                checkFloat(state, 3),
                checkFloat(state, 4)
        );
    }
    return pushHandle(state, handle, false);
}

int luaCreateLabel(lua_State* state) {
    return pushHandle(state, engine_imguiCreateLabel(
            checkHandle(state, 1),
            luaL_checkstring(state, 2),
            lua_isnoneornil(state, 3) || lua_toboolean(state, 3)
    ), false);
}

int luaCreateCheckBox(lua_State* state) {
    return pushHandle(state, engine_imguiCreateCheckBox(
            checkHandle(state, 1),
            luaL_checkstring(state, 2),
            !lua_isnoneornil(state, 3) && lua_toboolean(state, 3)
    ), false);
}

int luaCreateSwitch(lua_State* state) {
    return pushHandle(state, engine_imguiCreateSwitch(
            checkHandle(state, 1),
            luaL_checkstring(state, 2),
            !lua_isnoneornil(state, 3) && lua_toboolean(state, 3),
            optionalFloat(state, 4, 0.0F)
    ), false);
}

int luaCreateInputText(lua_State* state) {
    const char* value = lua_isnoneornil(state, 3) ? "" : luaL_checkstring(state, 3);
    int inputType = lua_isnoneornil(state, 4) ? 0 : checkInt(state, 4, "inputType");
    return pushHandle(state, engine_imguiCreateInputText(
            checkHandle(state, 1),
            luaL_checkstring(state, 2),
            value,
            inputType,
            optionalFloat(state, 5, 0.0F),
            optionalFloat(state, 6, 0.0F)
    ), false);
}

int luaCreateProgressBar(lua_State* state) {
    return pushHandle(state, engine_imguiCreateProgressBar(
            checkHandle(state, 1),
            checkFloat(state, 2),
            optionalFloat(state, 3, 0.0F),
            optionalFloat(state, 4, 0.0F)
    ), false);
}

int luaCreateSlider(lua_State* state) {
    return pushHandle(state, engine_imguiCreateSlider(
            checkHandle(state, 1),
            luaL_checkstring(state, 2),
            checkInt(state, 3, "min"),
            checkInt(state, 4, "max"),
            checkInt(state, 5, "initialPos"),
            optionalFloat(state, 6, -1.0F)
    ), true);
}

int luaCreateColorPicker(lua_State* state) {
    const char* title = lua_isnoneornil(state, 2) ? "Color" : luaL_checkstring(state, 2);
    std::uint32_t color = lua_isnoneornil(state, 3) ? 0xFF000000U : checkColor(state, 3);
    return pushHandle(state, engine_imguiCreateColorPicker(
            checkHandle(state, 1),
            title,
            color,
            optionalFloat(state, 4, 0.0F),
            optionalFloat(state, 5, 0.0F)
    ), true);
}

int luaGetInputText(lua_State* state) {
    const char* text = engine_imguiGetInputText(checkHandle(state, 1));
    if (text == nullptr) lua_pushnil(state); else lua_pushstring(state, text);
    return 1;
}

int luaSetInputText(lua_State* state) {
    engine_imguiSetInputText(checkHandle(state, 1), luaL_checkstring(state, 2));
    return 0;
}

int luaSetInputType(lua_State* state) {
    engine_imguiSetInputType(checkHandle(state, 1), checkInt(state, 2, "inputType"));
    return 0;
}

int luaSetChecked(lua_State* state) {
    engine_imguiSetChecked(checkHandle(state, 1), lua_toboolean(state, 2));
    return 0;
}

int luaIsChecked(lua_State* state) {
    int checked = 0;
    if (!engine_imguiIsChecked(checkHandle(state, 1), &checked)) {
        lua_pushnil(state);
    } else {
        lua_pushboolean(state, checked);
    }
    return 1;
}

int luaSetProgressBarPos(lua_State* state) {
    engine_imguiSetProgressBarPos(checkHandle(state, 1), checkFloat(state, 2));
    return 0;
}

int luaGetProgressBarPos(lua_State* state) {
    float value = 0.0F;
    if (!engine_imguiGetProgressBarPos(checkHandle(state, 1), &value)) lua_pushnil(state);
    else lua_pushnumber(state, value);
    return 1;
}

int luaSetSlider(lua_State* state) {
    engine_imguiSetSlider(checkHandle(state, 1), checkInt(state, 2, "pos"));
    return 0;
}

int luaGetSliderPos(lua_State* state) {
    int value = 0;
    if (!engine_imguiGetSliderPos(checkHandle(state, 1), &value)) lua_pushnil(state);
    else lua_pushinteger(state, value);
    return 1;
}

int luaSetWidgetSize(lua_State* state) {
    engine_imguiSetWidgetSize(checkHandle(state, 1), checkFloat(state, 2), checkFloat(state, 3));
    return 0;
}

int luaSetWidgetVisible(lua_State* state) {
    engine_imguiSetWidgetVisible(checkHandle(state, 1), lua_toboolean(state, 2));
    return 0;
}

int luaIsWidgetVisible(lua_State* state) {
    int visible = 0;
    if (!engine_imguiIsWidgetVisible(checkHandle(state, 1), &visible)) lua_pushboolean(state, 0);
    else lua_pushboolean(state, visible);
    return 1;
}

int luaSetWidgetStyle(lua_State* state) {
    engine_imguiSetWidgetStyle(
            checkHandle(state, 1),
            checkInt(state, 2, "style"),
            checkFloat(state, 3),
            optionalFloat(state, 4, 0.0F)
    );
    return 0;
}

int luaSetWidgetColor(lua_State* state) {
    engine_imguiSetWidgetColor(
            checkHandle(state, 1),
            checkInt(state, 2, "type"),
            checkColor(state, 3)
    );
    return 0;
}

/** 把竖线字符串或 Lua 数组转换为组合框选项，并支持反斜杠转义竖线。 */
std::vector<std::string> readItems(lua_State* state, int index) {
    std::vector<std::string> items;
    if (lua_istable(state, index)) {
        std::size_t count = lua_rawlen(state, index);
        items.reserve(count);
        for (std::size_t item = 1; item <= count; ++item) {
            lua_rawgeti(state, index, static_cast<lua_Integer>(item));
            items.emplace_back(luaL_checkstring(state, -1));
            lua_pop(state, 1);
        }
        return items;
    }

    const char* source = luaL_checkstring(state, index);
    std::string current;
    for (const char* cursor = source; *cursor != '\0'; ++cursor) {
        if (*cursor == '\\' && (cursor[1] == '|' || cursor[1] == '\\')) {
            // 只消费文档定义的 \| 和 \\；路径中的普通反斜杠必须原样保留。
            current.push_back(cursor[1]);
            ++cursor;
        } else if (*cursor == '|') {
            items.push_back(current);
            current.clear();
        } else {
            current.push_back(*cursor);
        }
    }
    items.push_back(current);
    return items;
}

/** 选择控件和表格绑定。 */
int luaCreateComboBox(lua_State* state) {
    std::vector<std::string> items = readItems(state, 2);
    std::vector<const char*> pointers;
    pointers.reserve(items.size());
    for (const std::string& item : items) pointers.push_back(item.c_str());
    return pushHandle(state, engine_imguiCreateComboBox(
            checkHandle(state, 1),
            pointers.empty() ? nullptr : pointers.data(),
            static_cast<int>(pointers.size()),
            optionalFloat(state, 3, 0.0F)
    ), false);
}

int luaCreateRadioGroup(lua_State* state) {
    return pushHandle(state, engine_imguiCreateRadioGroup(
            checkHandle(state, 1),
            luaL_checkstring(state, 2)
    ), true);
}

int luaAddOptionItem(lua_State* state) {
    engine_imguiAddOptionItem(checkHandle(state, 1), luaL_checkstring(state, 2));
    return 0;
}

int luaAddRadioBox(lua_State* state) {
    lua_pushboolean(state, engine_imguiAddRadioBox(
            checkHandle(state, 1),
            luaL_checkstring(state, 2),
            !lua_isnoneornil(state, 3) && lua_toboolean(state, 3)
    ));
    return 1;
}

int luaGetItemText(lua_State* state) {
    const char* text = engine_imguiGetItemText(
            checkHandle(state, 1),
            checkInt(state, 2, "item_index")
    );
    if (text == nullptr) lua_pushnil(state); else lua_pushstring(state, text);
    return 1;
}

int luaRemoveItemAt(lua_State* state) {
    engine_imguiRemoveItemAt(checkHandle(state, 1), checkInt(state, 2, "item_index"));
    return 0;
}

int luaRemoveAllItems(lua_State* state) {
    engine_imguiRemoveAllItems(checkHandle(state, 1));
    return 0;
}

int luaGetSelectedItemIndex(lua_State* state) {
    lua_pushinteger(state, engine_imguiGetSelectedItemIndex(checkHandle(state, 1)));
    return 1;
}

int luaSetItemSelected(lua_State* state) {
    engine_imguiSetItemSelected(checkHandle(state, 1), checkInt(state, 2, "index"));
    return 0;
}

int luaGetItemCount(lua_State* state) {
    lua_pushinteger(state, engine_imguiGetItemCount(checkHandle(state, 1)));
    return 1;
}

int luaCreateTableView(lua_State* state) {
    return pushHandle(state, engine_imguiCreateTableView(
            checkHandle(state, 1),
            luaL_checkstring(state, 2),
            checkInt(state, 3, "columns"),
            lua_toboolean(state, 4),
            optionalFloat(state, 5, -1.0F),
            optionalFloat(state, 6, -1.0F)
    ), true);
}

int luaSetTableHeaderItem(lua_State* state) {
    engine_imguiSetTableHeaderItem(
            checkHandle(state, 1),
            checkInt(state, 2, "col"),
            luaL_checkstring(state, 3)
    );
    return 0;
}

int luaInsertTableRow(lua_State* state) {
    int row = engine_imguiInsertTableRow(
            checkHandle(state, 1),
            checkInt(state, 2, "after")
    );
    if (row < 0) lua_pushnil(state); else lua_pushinteger(state, row);
    return 1;
}

int luaGetTableItemText(lua_State* state) {
    const char* text = engine_imguiGetTableItemText(
            checkHandle(state, 1),
            checkInt(state, 2, "row"),
            checkInt(state, 3, "col")
    );
    if (text == nullptr) lua_pushnil(state); else lua_pushstring(state, text);
    return 1;
}

int luaSetTableItemText(lua_State* state) {
    engine_imguiSetTableItemText(
            checkHandle(state, 1),
            checkInt(state, 2, "row"),
            checkInt(state, 3, "col"),
            luaL_checkstring(state, 4)
    );
    return 0;
}

int luaDeleteTableRow(lua_State* state) {
    engine_imguiDeleteTableRow(checkHandle(state, 1), checkInt(state, 2, "row"));
    return 0;
}

int luaClearTable(lua_State* state) {
    engine_imguiClearTable(checkHandle(state, 1));
    return 0;
}

/** 把 Java Bitmap userdata 复制为 RGBA；字符串参数由 C ABI 直接按图片路径加载。 */
bool readBitmap(lua_State* state, int index, std::vector<unsigned char>* rgba, int* width, int* height) {
    std::string error;
    if (copyLuaJavaBitmapRgba(state, index, rgba, width, height, &error)) {
        return true;
    }
    luaL_error(state, "%s", error.c_str());
    return false;
}

int luaCreateImage(lua_State* state) {
    const char* path = lua_isnoneornil(state, 2) ? "" : luaL_checkstring(state, 2);
    return pushHandle(state, engine_imguiCreateImage(
            checkHandle(state, 1),
            path,
            optionalFloat(state, 3, 0.0F),
            optionalFloat(state, 4, 0.0F)
    ), true);
}

int luaSetImage(lua_State* state) {
    const char* path = lua_isnoneornil(state, 2) ? "" : luaL_checkstring(state, 2);
    engine_imguiSetImage(checkHandle(state, 1), path);
    return 0;
}

int luaSetImageFromBitmap(lua_State* state) {
    std::vector<unsigned char> rgba;
    int width = 0;
    int height = 0;
    readBitmap(state, 2, &rgba, &width, &height);
    engine_imguiSetImageRgba(checkHandle(state, 1), rgba.data(), width, height);
    return 0;
}

/**
 * 读取多边形顶点。
 *
 * 同时接受懒人精灵常见的 {{x,y},...}、{{x=,y=},...}，以及便于动态生成的
 * {x1,y1,x2,y2,...} 平面数组；返回前已经完成所有 Lua 栈读取。
 */
std::vector<EngineImGuiPointF> readPolygonPoints(lua_State* state, int index) {
    luaL_checktype(state, index, LUA_TTABLE);
    index = lua_absindex(state, index);
    std::size_t count = lua_rawlen(state, index);
    std::vector<EngineImGuiPointF> points;
    if (count > 0) {
        lua_rawgeti(state, index, 1);
        const bool nested = lua_istable(state, -1);
        lua_pop(state, 1);
        if (!nested) {
            luaL_argcheck(state, count % 2 == 0, index, "平面顶点数组长度必须是偶数");
            points.reserve(count / 2);
            for (std::size_t position = 1; position <= count; position += 2) {
                lua_rawgeti(state, index, static_cast<lua_Integer>(position));
                lua_rawgeti(state, index, static_cast<lua_Integer>(position + 1));
                points.push_back({checkFloat(state, -2), checkFloat(state, -1)});
                lua_pop(state, 2);
            }
            return points;
        }
    }

    points.reserve(count);
    for (std::size_t position = 1; position <= count; ++position) {
        lua_rawgeti(state, index, static_cast<lua_Integer>(position));
        luaL_checktype(state, -1, LUA_TTABLE);
        int pointTable = lua_absindex(state, -1);
        EngineImGuiPointF point{};
        lua_getfield(state, pointTable, "x");
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            lua_rawgeti(state, pointTable, 1);
        }
        point.x = static_cast<float>(luaL_checknumber(state, -1));
        lua_pop(state, 1);
        lua_getfield(state, pointTable, "y");
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            lua_rawgeti(state, pointTable, 2);
        }
        point.y = static_cast<float>(luaL_checknumber(state, -1));
        lua_pop(state, 1);
        lua_pop(state, 1);
        points.push_back(point);
    }
    return points;
}

/** 绘图图形绑定。 */
int luaCreateRectangle(lua_State* state) {
    return pushHandle(state, engine_imguiCreateRectangle(
            checkFloat(state, 1), checkFloat(state, 2),
            checkFloat(state, 3), checkFloat(state, 4),
            checkColor(state, 5), lua_toboolean(state, 6), checkFloat(state, 7)
    ), true);
}

int luaCreateCircle(lua_State* state) {
    return pushHandle(state, engine_imguiCreateCircle(
            checkFloat(state, 1), checkFloat(state, 2), checkFloat(state, 3),
            checkColor(state, 4), lua_toboolean(state, 5), checkInt(state, 6, "segments")
    ), true);
}

int luaCreatePolygon(lua_State* state) {
    std::vector<EngineImGuiPointF> points = readPolygonPoints(state, 1);
    return pushHandle(state, engine_imguiCreatePolygon(
            points.data(),
            static_cast<int>(points.size()),
            checkColor(state, 2),
            lua_toboolean(state, 3),
            lua_toboolean(state, 4),
            checkFloat(state, 5)
    ), true);
}

int luaCreateLine(lua_State* state) {
    return pushHandle(state, engine_imguiCreateLine(
            checkFloat(state, 1), checkFloat(state, 2),
            checkFloat(state, 3), checkFloat(state, 4),
            checkColor(state, 5), checkFloat(state, 6)
    ), true);
}

int luaCreateBitmapShape(lua_State* state) {
    float x = checkFloat(state, 1);
    float y = checkFloat(state, 2);
    float width = checkFloat(state, 3);
    float height = checkFloat(state, 4);
    if (lua_type(state, 5) == LUA_TSTRING) {
        return pushHandle(state, engine_imguiCreateBitmapShape(
                x, y, width, height, lua_tostring(state, 5)
        ), true);
    }
    std::vector<unsigned char> rgba;
    int imageWidth = 0;
    int imageHeight = 0;
    readBitmap(state, 5, &rgba, &imageWidth, &imageHeight);
    return pushHandle(state, engine_imguiCreateBitmapShapeRgba(
            x, y, width, height, rgba.data(), imageWidth, imageHeight
    ), true);
}

int luaCreateShapeText(lua_State* state) {
    return pushHandle(state, engine_imguiCreateShapeText(
            checkFloat(state, 1), checkFloat(state, 2),
            checkFloat(state, 3), checkFloat(state, 4),
            luaL_checkstring(state, 5), checkColor(state, 6), checkColor(state, 7),
            lua_toboolean(state, 8), checkFloat(state, 9)
    ), true);
}

int luaSetShapePosition(lua_State* state) {
    lua_pushinteger(state, engine_imguiSetShapePosition(
            checkHandle(state, 1), checkFloat(state, 2), checkFloat(state, 3)
    ));
    return 1;
}

int luaSetShapeVisibility(lua_State* state) {
    lua_pushinteger(state, engine_imguiSetShapeVisibility(
            checkHandle(state, 1), lua_toboolean(state, 2)
    ));
    return 1;
}

int luaIsShapeVisibility(lua_State* state) {
    int visible = 0;
    if (!engine_imguiIsShapeVisibility(checkHandle(state, 1), &visible)) {
        lua_pushboolean(state, 0);
    } else {
        lua_pushboolean(state, visible);
    }
    return 1;
}

int luaSetShapeTextString(lua_State* state) {
    lua_pushboolean(state, engine_imguiSetShapeTextString(
            checkHandle(state, 1), luaL_checkstring(state, 2)
    ));
    return 1;
}

int luaSetShapeTextColor(lua_State* state) {
    lua_pushboolean(state, engine_imguiSetShapeTextColor(
            checkHandle(state, 1), checkColor(state, 2)
    ));
    return 1;
}

int luaSetShapeTextBackground(lua_State* state) {
    lua_pushboolean(state, engine_imguiSetShapeTextBackground(
            checkHandle(state, 1), checkColor(state, 2), lua_toboolean(state, 3)
    ));
    return 1;
}

int luaSetShapeTextFontScale(lua_State* state) {
    lua_pushboolean(state, engine_imguiSetShapeTextFontScale(
            checkHandle(state, 1), checkFloat(state, 2)
    ));
    return 1;
}

int luaSetBitmapShape(lua_State* state) {
    EngineImGuiHandle handle = checkHandle(state, 1);
    if (lua_type(state, 2) == LUA_TSTRING) {
        engine_imguiSetBitmapShape(handle, lua_tostring(state, 2));
        return 0;
    }
    std::vector<unsigned char> rgba;
    int width = 0;
    int height = 0;
    readBitmap(state, 2, &rgba, &width, &height);
    engine_imguiSetBitmapShapeRgba(handle, rgba.data(), width, height);
    return 0;
}

int luaSetShapeThickness(lua_State* state) {
    lua_pushboolean(state, engine_imguiSetShapeThickness(
            checkHandle(state, 1), checkFloat(state, 2)
    ));
    return 1;
}

int luaRemoveShape(lua_State* state) {
    lua_pushinteger(state, engine_imguiRemoveShape(checkHandle(state, 1)));
    return 1;
}

int luaIsValidHandle(lua_State* state) {
    lua_pushboolean(state, engine_imguiIsValidHandle(checkHandle(state, 1)));
    return 1;
}

/** 回调注册绑定；nil 会删除原回调。 */
int luaSetOnClick(lua_State* state) {
    EngineImGuiHandle handle = checkHandle(state, 1);
    setCallback(state, "click", handle, 2);
    return 0;
}

int luaSetOnCheck(lua_State* state) {
    EngineImGuiHandle handle = checkHandle(state, 1);
    setCallback(state, "check", handle, 2);
    return 0;
}

int luaSetOnSelectEvent(lua_State* state) {
    EngineImGuiHandle handle = checkHandle(state, 1);
    setCallback(state, "select", handle, 2);
    return 0;
}

int luaSetOnSelectEventEx(lua_State* state) {
    EngineImGuiHandle handle = checkHandle(state, 1);
    setCallback(state, "table", handle, 2);
    return 0;
}

int luaSetOnSliderEvent(lua_State* state) {
    EngineImGuiHandle handle = checkHandle(state, 1);
    setCallback(state, "slider", handle, 2);
    return 0;
}

int luaSetOnClose(lua_State* state) {
    EngineImGuiHandle handle = checkHandle(state, 1);
    setCallback(state, "close", handle, 2);
    return 0;
}

int luaPost(lua_State* state) {
    luaL_checktype(state, 1, LUA_TFUNCTION);
    long long postId = gNextPostId.fetch_add(1);
    if (postId <= 0) {
        gNextPostId.store(2);
        postId = 1;
    }
    setCallback(state, "post", postId, 1);
    const bool posted = engine_imguiPost(postId) != 0;
    if (!posted) {
        // 投递失败时不会再产生事件，立即释放刚保存的函数引用，避免长脚本累计无效回调。
        pushCallbackTable(state);
        lua_pushnil(state);
        lua_setfield(state, -2, callbackKey("post", postId).c_str());
        lua_pop(state, 1);
    }
    lua_pushboolean(state, posted);
    return 1;
}

void setFunction(lua_State* state, int table, const char* name, lua_CFunction function) {
    lua_pushcfunction(state, function);
    lua_setfield(state, table, name);
}

void setInteger(lua_State* state, int table, const char* name, lua_Integer value) {
    lua_pushinteger(state, value);
    lua_setfield(state, table, name);
}

struct NamedInteger {
    const char* name;
    lua_Integer value;
};

/** 创建只读约定使用的整数常量表；Lua 仍允许脚本按正常 table 语义自行扩展。 */
void setIntegerTable(
        lua_State* state,
        int parent,
        const char* name,
        std::initializer_list<NamedInteger> values
) {
    lua_newtable(state);
    const int table = lua_gettop(state);
    for (const NamedInteger& value : values) {
        setInteger(state, table, value.name, value.value);
    }
    lua_setfield(state, parent, name);
}

/**
 * 给脚本提供输入类型、主题及 Dear ImGui 公共枚举。
 *
 * setWidgetStyle/setWidgetColor/setWindowFlags 的数值必须与编入 SO 的 Dear ImGui 版本
 * 完全一致，因此直接从 v1.92.8 头文件取值，脚本不需要抄写易变的魔法数字。
 */
void registerConstants(lua_State* state, int imguiTable) {
    setIntegerTable(state, imguiTable, "InputType", {
            {"Text", 0},
            {"Password", 1},
            {"Multiline", 2},
    });
    setIntegerTable(state, imguiTable, "Theme", {
            {"Dark", 0},
            {"Light", 1},
            {"Classic", 2},
    });
    setIntegerTable(state, imguiTable, "WindowFlags", {
            {"None", ImGuiWindowFlags_None},
            {"NoTitleBar", ImGuiWindowFlags_NoTitleBar},
            {"NoResize", ImGuiWindowFlags_NoResize},
            {"NoMove", ImGuiWindowFlags_NoMove},
            {"NoScrollbar", ImGuiWindowFlags_NoScrollbar},
            {"NoScrollWithMouse", ImGuiWindowFlags_NoScrollWithMouse},
            {"NoCollapse", ImGuiWindowFlags_NoCollapse},
            {"AlwaysAutoResize", ImGuiWindowFlags_AlwaysAutoResize},
            {"NoBackground", ImGuiWindowFlags_NoBackground},
            {"NoSavedSettings", ImGuiWindowFlags_NoSavedSettings},
            {"NoMouseInputs", ImGuiWindowFlags_NoMouseInputs},
            {"MenuBar", ImGuiWindowFlags_MenuBar},
            {"HorizontalScrollbar", ImGuiWindowFlags_HorizontalScrollbar},
            {"NoFocusOnAppearing", ImGuiWindowFlags_NoFocusOnAppearing},
            {"NoBringToFrontOnFocus", ImGuiWindowFlags_NoBringToFrontOnFocus},
            {"AlwaysVerticalScrollbar", ImGuiWindowFlags_AlwaysVerticalScrollbar},
            {"AlwaysHorizontalScrollbar", ImGuiWindowFlags_AlwaysHorizontalScrollbar},
            {"NoNavInputs", ImGuiWindowFlags_NoNavInputs},
            {"NoNavFocus", ImGuiWindowFlags_NoNavFocus},
            {"UnsavedDocument", ImGuiWindowFlags_UnsavedDocument},
            {"NoNav", ImGuiWindowFlags_NoNav},
            {"NoDecoration", ImGuiWindowFlags_NoDecoration},
            {"NoInputs", ImGuiWindowFlags_NoInputs},
    });
    lua_getfield(state, imguiTable, "WindowFlags");
    lua_setfield(state, imguiTable, "ImGuiWindowFlags");

    setIntegerTable(state, imguiTable, "StyleVar", {
            {"Alpha", ImGuiStyleVar_Alpha},
            {"DisabledAlpha", ImGuiStyleVar_DisabledAlpha},
            {"WindowPadding", ImGuiStyleVar_WindowPadding},
            {"WindowRounding", ImGuiStyleVar_WindowRounding},
            {"WindowBorderSize", ImGuiStyleVar_WindowBorderSize},
            {"WindowMinSize", ImGuiStyleVar_WindowMinSize},
            {"WindowTitleAlign", ImGuiStyleVar_WindowTitleAlign},
            {"ChildRounding", ImGuiStyleVar_ChildRounding},
            {"ChildBorderSize", ImGuiStyleVar_ChildBorderSize},
            {"PopupRounding", ImGuiStyleVar_PopupRounding},
            {"PopupBorderSize", ImGuiStyleVar_PopupBorderSize},
            {"FramePadding", ImGuiStyleVar_FramePadding},
            {"FrameRounding", ImGuiStyleVar_FrameRounding},
            {"FrameBorderSize", ImGuiStyleVar_FrameBorderSize},
            {"ItemSpacing", ImGuiStyleVar_ItemSpacing},
            {"ItemInnerSpacing", ImGuiStyleVar_ItemInnerSpacing},
            {"IndentSpacing", ImGuiStyleVar_IndentSpacing},
            {"CellPadding", ImGuiStyleVar_CellPadding},
            {"ScrollbarSize", ImGuiStyleVar_ScrollbarSize},
            {"ScrollbarRounding", ImGuiStyleVar_ScrollbarRounding},
            {"ScrollbarPadding", ImGuiStyleVar_ScrollbarPadding},
            {"GrabMinSize", ImGuiStyleVar_GrabMinSize},
            {"GrabRounding", ImGuiStyleVar_GrabRounding},
            {"ImageRounding", ImGuiStyleVar_ImageRounding},
            {"ImageBorderSize", ImGuiStyleVar_ImageBorderSize},
            {"TabRounding", ImGuiStyleVar_TabRounding},
            {"TabBorderSize", ImGuiStyleVar_TabBorderSize},
            {"TabMinWidthBase", ImGuiStyleVar_TabMinWidthBase},
            {"TabMinWidthShrink", ImGuiStyleVar_TabMinWidthShrink},
            {"TabBarBorderSize", ImGuiStyleVar_TabBarBorderSize},
            {"TabBarOverlineSize", ImGuiStyleVar_TabBarOverlineSize},
            {"TableAngledHeadersAngle", ImGuiStyleVar_TableAngledHeadersAngle},
            {"TableAngledHeadersTextAlign", ImGuiStyleVar_TableAngledHeadersTextAlign},
            {"TreeLinesSize", ImGuiStyleVar_TreeLinesSize},
            {"TreeLinesRounding", ImGuiStyleVar_TreeLinesRounding},
            {"DragDropTargetRounding", ImGuiStyleVar_DragDropTargetRounding},
            {"ButtonTextAlign", ImGuiStyleVar_ButtonTextAlign},
            {"SelectableTextAlign", ImGuiStyleVar_SelectableTextAlign},
            {"SeparatorSize", ImGuiStyleVar_SeparatorSize},
            {"SeparatorTextBorderSize", ImGuiStyleVar_SeparatorTextBorderSize},
            {"SeparatorTextAlign", ImGuiStyleVar_SeparatorTextAlign},
            {"SeparatorTextPadding", ImGuiStyleVar_SeparatorTextPadding},
    });
    lua_getfield(state, imguiTable, "StyleVar");
    lua_setfield(state, imguiTable, "ImGuiStyleVar");

    setIntegerTable(state, imguiTable, "Color", {
            {"Text", ImGuiCol_Text},
            {"TextDisabled", ImGuiCol_TextDisabled},
            {"WindowBg", ImGuiCol_WindowBg},
            {"ChildBg", ImGuiCol_ChildBg},
            {"PopupBg", ImGuiCol_PopupBg},
            {"Border", ImGuiCol_Border},
            {"BorderShadow", ImGuiCol_BorderShadow},
            {"FrameBg", ImGuiCol_FrameBg},
            {"FrameBgHovered", ImGuiCol_FrameBgHovered},
            {"FrameBgActive", ImGuiCol_FrameBgActive},
            {"TitleBg", ImGuiCol_TitleBg},
            {"TitleBgActive", ImGuiCol_TitleBgActive},
            {"TitleBgCollapsed", ImGuiCol_TitleBgCollapsed},
            {"MenuBarBg", ImGuiCol_MenuBarBg},
            {"ScrollbarBg", ImGuiCol_ScrollbarBg},
            {"ScrollbarGrab", ImGuiCol_ScrollbarGrab},
            {"ScrollbarGrabHovered", ImGuiCol_ScrollbarGrabHovered},
            {"ScrollbarGrabActive", ImGuiCol_ScrollbarGrabActive},
            {"CheckMark", ImGuiCol_CheckMark},
            {"CheckboxSelectedBg", ImGuiCol_CheckboxSelectedBg},
            {"SliderGrab", ImGuiCol_SliderGrab},
            {"SliderGrabActive", ImGuiCol_SliderGrabActive},
            {"Button", ImGuiCol_Button},
            {"ButtonHovered", ImGuiCol_ButtonHovered},
            {"ButtonActive", ImGuiCol_ButtonActive},
            {"Header", ImGuiCol_Header},
            {"HeaderHovered", ImGuiCol_HeaderHovered},
            {"HeaderActive", ImGuiCol_HeaderActive},
            {"Separator", ImGuiCol_Separator},
            {"SeparatorHovered", ImGuiCol_SeparatorHovered},
            {"SeparatorActive", ImGuiCol_SeparatorActive},
            {"ResizeGrip", ImGuiCol_ResizeGrip},
            {"ResizeGripHovered", ImGuiCol_ResizeGripHovered},
            {"ResizeGripActive", ImGuiCol_ResizeGripActive},
            {"InputTextCursor", ImGuiCol_InputTextCursor},
            {"TabHovered", ImGuiCol_TabHovered},
            {"Tab", ImGuiCol_Tab},
            {"TabSelected", ImGuiCol_TabSelected},
            {"TabSelectedOverline", ImGuiCol_TabSelectedOverline},
            {"TabDimmed", ImGuiCol_TabDimmed},
            {"TabDimmedSelected", ImGuiCol_TabDimmedSelected},
            {"TabDimmedSelectedOverline", ImGuiCol_TabDimmedSelectedOverline},
            {"PlotLines", ImGuiCol_PlotLines},
            {"PlotLinesHovered", ImGuiCol_PlotLinesHovered},
            {"PlotHistogram", ImGuiCol_PlotHistogram},
            {"PlotHistogramHovered", ImGuiCol_PlotHistogramHovered},
            {"TableHeaderBg", ImGuiCol_TableHeaderBg},
            {"TableBorderStrong", ImGuiCol_TableBorderStrong},
            {"TableBorderLight", ImGuiCol_TableBorderLight},
            {"TableRowBg", ImGuiCol_TableRowBg},
            {"TableRowBgAlt", ImGuiCol_TableRowBgAlt},
            {"TextLink", ImGuiCol_TextLink},
            {"TextSelectedBg", ImGuiCol_TextSelectedBg},
            {"TreeLines", ImGuiCol_TreeLines},
            {"DragDropTarget", ImGuiCol_DragDropTarget},
            {"DragDropTargetBg", ImGuiCol_DragDropTargetBg},
            {"UnsavedMarker", ImGuiCol_UnsavedMarker},
            {"NavCursor", ImGuiCol_NavCursor},
            {"NavWindowingHighlight", ImGuiCol_NavWindowingHighlight},
            {"NavWindowingDimBg", ImGuiCol_NavWindowingDimBg},
            {"ModalWindowDimBg", ImGuiCol_ModalWindowDimBg},
    });
    lua_getfield(state, imguiTable, "Color");
    lua_pushvalue(state, -1);
    lua_setfield(state, imguiTable, "ImGuiColor");
    lua_setfield(state, imguiTable, "ImGuiCol");
}

} // namespace

void registerLuaImGuiApi(lua_State* state) {
    lua_newtable(state);
    int table = lua_gettop(state);

    setFunction(state, table, "isSupport", luaIsSupport);
    setFunction(state, table, "getLastError", luaGetLastError);
    setFunction(state, table, "isValidHandle", luaIsValidHandle);
    setFunction(state, table, "show", luaShow);
    setFunction(state, table, "showWindow", luaShowWindow);
    setFunction(state, table, "close", luaClose);
    setFunction(state, table, "setColorTheme", luaSetColorTheme);
    setFunction(state, table, "post", luaPost);

    setFunction(state, table, "createWindow", luaCreateWindow);
    setFunction(state, table, "destroyWindow", luaDestroyWindow);
    setFunction(state, table, "setOnClose", luaSetOnClose);
    setFunction(state, table, "setWindowPos", luaSetWindowPos);
    setFunction(state, table, "setWindowSize", luaSetWindowSize);
    setFunction(state, table, "getWindowPos", luaGetWindowPos);
    setFunction(state, table, "setWindowFlags", luaSetWindowFlags);
    setFunction(state, table, "createVerticalLayout", luaCreateVerticalLayout);
    setFunction(state, table, "createHorticalLayout", luaCreateHorticalLayout);
    setFunction(state, table, "createTreeBoxLayout", luaCreateTreeBoxLayout);
    setFunction(state, table, "createTabBar", luaCreateTabBar);
    setFunction(state, table, "addTabBarItem", luaAddTabBarItem);
    setFunction(state, table, "sameLine", luaSameLine);
    setFunction(state, table, "setLayoutBorderVisible", luaSetLayoutBorderVisible);

    setFunction(state, table, "createButton", luaCreateButton);
    setFunction(state, table, "setOnClick", luaSetOnClick);
    setFunction(state, table, "createLabel", luaCreateLabel);
    setFunction(state, table, "createCheckBox", luaCreateCheckBox);
    setFunction(state, table, "createSwitch", luaCreateSwitch);
    setFunction(state, table, "setChecked", luaSetChecked);
    setFunction(state, table, "isChecked", luaIsChecked);
    setFunction(state, table, "setOnCheck", luaSetOnCheck);
    setFunction(state, table, "createInputText", luaCreateInputText);
    setFunction(state, table, "getInputText", luaGetInputText);
    setFunction(state, table, "setInputText", luaSetInputText);
    setFunction(state, table, "setInputType", luaSetInputType);
    setFunction(state, table, "createProgressBar", luaCreateProgressBar);
    setFunction(state, table, "setProgressBarPos", luaSetProgressBarPos);
    setFunction(state, table, "getProgressBarPos", luaGetProgressBarPos);
    setFunction(state, table, "createSlider", luaCreateSlider);
    setFunction(state, table, "setSlider", luaSetSlider);
    setFunction(state, table, "getSliderPos", luaGetSliderPos);
    setFunction(state, table, "setOnSliderEvent", luaSetOnSliderEvent);
    setFunction(state, table, "createColorPicker", luaCreateColorPicker);

    setFunction(state, table, "createComboBox", luaCreateComboBox);
    setFunction(state, table, "addOptionItem", luaAddOptionItem);
    setFunction(state, table, "getItemText", luaGetItemText);
    setFunction(state, table, "removeItemAt", luaRemoveItemAt);
    setFunction(state, table, "removeAllItems", luaRemoveAllItems);
    setFunction(state, table, "getSelectedItemIndex", luaGetSelectedItemIndex);
    setFunction(state, table, "setItemSelected", luaSetItemSelected);
    setFunction(state, table, "setOnSelectEvent", luaSetOnSelectEvent);
    setFunction(state, table, "createRadioGroup", luaCreateRadioGroup);
    setFunction(state, table, "addRadioBox", luaAddRadioBox);
    setFunction(state, table, "getItemCount", luaGetItemCount);
    setFunction(state, table, "createTableView", luaCreateTableView);
    setFunction(state, table, "setTableHeaderItem", luaSetTableHeaderItem);
    setFunction(state, table, "insertTableRow", luaInsertTableRow);
    setFunction(state, table, "getTableItemText", luaGetTableItemText);
    setFunction(state, table, "setTableItemText", luaSetTableItemText);
    setFunction(state, table, "deleteTableRow", luaDeleteTableRow);
    setFunction(state, table, "clearTable", luaClearTable);
    setFunction(state, table, "setOnSelectEventEx", luaSetOnSelectEventEx);

    setFunction(state, table, "setWidgetSize", luaSetWidgetSize);
    setFunction(state, table, "setWidgetVisible", luaSetWidgetVisible);
    setFunction(state, table, "isWidgetVisible", luaIsWidgetVisible);
    setFunction(state, table, "setWidgetStyle", luaSetWidgetStyle);
    setFunction(state, table, "setWidgetColor", luaSetWidgetColor);
    setFunction(state, table, "createImage", luaCreateImage);
    setFunction(state, table, "setImage", luaSetImage);
    setFunction(state, table, "setImageFromBitmap", luaSetImageFromBitmap);

    setFunction(state, table, "createRectangle", luaCreateRectangle);
    setFunction(state, table, "createCircle", luaCreateCircle);
    setFunction(state, table, "createPolygon", luaCreatePolygon);
    setFunction(state, table, "createLine", luaCreateLine);
    setFunction(state, table, "createBitmapShape", luaCreateBitmapShape);
    setFunction(state, table, "createShapeText", luaCreateShapeText);
    setFunction(state, table, "setShapePosition", luaSetShapePosition);
    setFunction(state, table, "setShapeVisibility", luaSetShapeVisibility);
    setFunction(state, table, "isShapeVisibility", luaIsShapeVisibility);
    setFunction(state, table, "setShapeTextString", luaSetShapeTextString);
    setFunction(state, table, "setShapeTextColor", luaSetShapeTextColor);
    setFunction(state, table, "setShapeTextBackground", luaSetShapeTextBackground);
    setFunction(state, table, "setShapeTextFontScale", luaSetShapeTextFontScale);
    setFunction(state, table, "setBitmapShape", luaSetBitmapShape);
    setFunction(state, table, "setShapeThickness", luaSetShapeThickness);
    setFunction(state, table, "removeShape", luaRemoveShape);

    registerConstants(state, table);
    lua_setglobal(state, "imgui");
}
