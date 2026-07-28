/**
 * 文件用途：实现懒人 cv.new/get/set/delete 指针接口，并提供可传给 FFI 的稳定值内存。
 */
#include "cv_compat_lua_api.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

namespace {

constexpr const char* kPointerMetatable = "xiaoyv.CvCompatPointer";

enum class PointerKind : unsigned char {
    Point,
    Point2f,
    Int,
    Double,
    Float,
    Long,
    Byte,
};

/**
 * storage 必须位于 userdata 首地址。
 *
 * ffi 把完整 userdata 作为 void* 传入 C 函数时，调用方看到的正是对应 Point、标量值，
 * 而不是内部元数据。kind/alive 只供 Lua 侧类型与生命周期检查使用。
 */
struct CvPointer {
    union Storage {
        struct {
            std::int32_t x;
            std::int32_t y;
        } point;
        struct {
            float x;
            float y;
        } point2f;
        std::int32_t intValue;
        double doubleValue;
        float floatValue;
        std::int64_t longValue;
        std::uint8_t byteValue;

        Storage() : longValue(0) {
        }
    } storage;
    PointerKind kind = PointerKind::Int;
    bool alive = true;
};

static_assert(offsetof(CvPointer, storage) == 0, "cv userdata 的值必须位于首地址");

CvPointer* newPointer(lua_State* state, PointerKind kind) {
    void* memory = lua_newuserdatauv(state, sizeof(CvPointer), 0);
    auto* pointer = new(memory) CvPointer();
    pointer->kind = kind;
    pointer->alive = true;
    luaL_setmetatable(state, kPointerMetatable);
    return pointer;
}

CvPointer* checkPointer(lua_State* state, int index, PointerKind expected) {
    auto* pointer = static_cast<CvPointer*>(
            luaL_checkudata(state, index, kPointerMetatable)
    );
    if (!pointer->alive) {
        luaL_error(state, "cv 指针已释放");
        return nullptr;
    }
    if (pointer->kind != expected) {
        luaL_error(state, "cv 指针类型不匹配");
        return nullptr;
    }
    return pointer;
}

std::int32_t checkInt32(lua_State* state, int index) {
    lua_Integer value = luaL_checkinteger(state, index);
    if (value < std::numeric_limits<std::int32_t>::min()
            || value > std::numeric_limits<std::int32_t>::max()) {
        luaL_argerror(state, index, "整数超出 int32 范围");
    }
    return static_cast<std::int32_t>(value);
}

float checkFloat(lua_State* state, int index) {
    lua_Number value = luaL_checknumber(state, index);
    if (!std::isfinite(static_cast<double>(value))
            || value < -std::numeric_limits<float>::max()
            || value > std::numeric_limits<float>::max()) {
        luaL_argerror(state, index, "数值超出 float 范围");
    }
    return static_cast<float>(value);
}

int newPoint(lua_State* state) {
    CvPointer* pointer = newPointer(state, PointerKind::Point);
    pointer->storage.point.x = checkInt32(state, 1);
    pointer->storage.point.y = checkInt32(state, 2);
    return 1;
}

int getPoint(lua_State* state) {
    CvPointer* pointer = checkPointer(state, 1, PointerKind::Point);
    lua_createtable(state, 0, 2);
    lua_pushinteger(state, pointer->storage.point.x);
    lua_setfield(state, -2, "x");
    lua_pushinteger(state, pointer->storage.point.y);
    lua_setfield(state, -2, "y");
    return 1;
}

int setPoint(lua_State* state) {
    CvPointer* pointer = checkPointer(state, 1, PointerKind::Point);
    pointer->storage.point.x = checkInt32(state, 2);
    pointer->storage.point.y = checkInt32(state, 3);
    return 0;
}

int newPoint2f(lua_State* state) {
    CvPointer* pointer = newPointer(state, PointerKind::Point2f);
    pointer->storage.point2f.x = checkFloat(state, 1);
    pointer->storage.point2f.y = checkFloat(state, 2);
    return 1;
}

int getPoint2f(lua_State* state) {
    CvPointer* pointer = checkPointer(state, 1, PointerKind::Point2f);
    lua_createtable(state, 0, 2);
    lua_pushnumber(state, pointer->storage.point2f.x);
    lua_setfield(state, -2, "x");
    lua_pushnumber(state, pointer->storage.point2f.y);
    lua_setfield(state, -2, "y");
    return 1;
}

int setPoint2f(lua_State* state) {
    CvPointer* pointer = checkPointer(state, 1, PointerKind::Point2f);
    pointer->storage.point2f.x = checkFloat(state, 2);
    pointer->storage.point2f.y = checkFloat(state, 3);
    return 0;
}

int newInt(lua_State* state) {
    CvPointer* pointer = newPointer(state, PointerKind::Int);
    pointer->storage.intValue = checkInt32(state, 1);
    return 1;
}

int getInt(lua_State* state) {
    lua_pushinteger(state, checkPointer(state, 1, PointerKind::Int)->storage.intValue);
    return 1;
}

int setInt(lua_State* state) {
    checkPointer(state, 1, PointerKind::Int)->storage.intValue = checkInt32(state, 2);
    return 0;
}

int newDouble(lua_State* state) {
    CvPointer* pointer = newPointer(state, PointerKind::Double);
    pointer->storage.doubleValue = luaL_checknumber(state, 1);
    return 1;
}

int getDouble(lua_State* state) {
    lua_pushnumber(
            state,
            checkPointer(state, 1, PointerKind::Double)->storage.doubleValue
    );
    return 1;
}

int setDouble(lua_State* state) {
    checkPointer(state, 1, PointerKind::Double)->storage.doubleValue =
            luaL_checknumber(state, 2);
    return 0;
}

int newFloat(lua_State* state) {
    CvPointer* pointer = newPointer(state, PointerKind::Float);
    pointer->storage.floatValue = checkFloat(state, 1);
    return 1;
}

int getFloat(lua_State* state) {
    lua_pushnumber(state, checkPointer(state, 1, PointerKind::Float)->storage.floatValue);
    return 1;
}

int setFloat(lua_State* state) {
    checkPointer(state, 1, PointerKind::Float)->storage.floatValue = checkFloat(state, 2);
    return 0;
}

int newLong(lua_State* state) {
    CvPointer* pointer = newPointer(state, PointerKind::Long);
    pointer->storage.longValue = static_cast<std::int64_t>(luaL_checkinteger(state, 1));
    return 1;
}

int getLong(lua_State* state) {
    lua_pushinteger(
            state,
            static_cast<lua_Integer>(
                    checkPointer(state, 1, PointerKind::Long)->storage.longValue
            )
    );
    return 1;
}

int setLong(lua_State* state) {
    checkPointer(state, 1, PointerKind::Long)->storage.longValue =
            static_cast<std::int64_t>(luaL_checkinteger(state, 2));
    return 0;
}

std::uint8_t checkByte(lua_State* state, int index) {
    lua_Integer value = luaL_checkinteger(state, index);
    if (value < 0 || value > 255) {
        luaL_argerror(state, index, "Byte 必须在 0 到 255 之间");
    }
    return static_cast<std::uint8_t>(value);
}

int newByte(lua_State* state) {
    CvPointer* pointer = newPointer(state, PointerKind::Byte);
    pointer->storage.byteValue = checkByte(state, 1);
    return 1;
}

int getByte(lua_State* state) {
    lua_pushinteger(state, checkPointer(state, 1, PointerKind::Byte)->storage.byteValue);
    return 1;
}

int setByte(lua_State* state) {
    checkPointer(state, 1, PointerKind::Byte)->storage.byteValue = checkByte(state, 2);
    return 0;
}

int deletePointer(lua_State* state) {
    auto* pointer = static_cast<CvPointer*>(
            luaL_checkudata(state, 1, kPointerMetatable)
    );
    pointer->alive = false;
    pointer->storage.longValue = 0;
    return 0;
}

int pointerGc(lua_State* state) {
    auto* pointer = static_cast<CvPointer*>(
            luaL_checkudata(state, 1, kPointerMetatable)
    );
    pointer->alive = false;
    pointer->~CvPointer();
    return 0;
}

void setFunction(lua_State* state, int tableIndex, const char* name, lua_CFunction function) {
    int absolute = lua_absindex(state, tableIndex);
    lua_pushcfunction(state, function);
    lua_setfield(state, absolute, name);
}

} // namespace

void registerCvCompatLuaApi(lua_State* state, int hostTableIndex) {
    if (luaL_newmetatable(state, kPointerMetatable) != 0) {
        setFunction(state, -1, "__gc", pointerGc);
        lua_pushliteral(state, "cv.pointer");
        lua_setfield(state, -2, "__name");
    }
    lua_pop(state, 1);

    lua_newtable(state);
    int table = lua_gettop(state);
    setFunction(state, table, "newPoint", newPoint);
    setFunction(state, table, "getPoint", getPoint);
    setFunction(state, table, "setPoint", setPoint);
    setFunction(state, table, "newPoint2f", newPoint2f);
    setFunction(state, table, "getPoint2f", getPoint2f);
    setFunction(state, table, "setPoint2f", setPoint2f);
    setFunction(state, table, "newInt", newInt);
    setFunction(state, table, "getInt", getInt);
    setFunction(state, table, "setInt", setInt);
    setFunction(state, table, "newDouble", newDouble);
    setFunction(state, table, "getDouble", getDouble);
    setFunction(state, table, "setDouble", setDouble);
    setFunction(state, table, "newFloat", newFloat);
    setFunction(state, table, "getFloat", getFloat);
    setFunction(state, table, "setFloat", setFloat);
    setFunction(state, table, "newLong", newLong);
    setFunction(state, table, "getLong", getLong);
    setFunction(state, table, "setLong", setLong);
    setFunction(state, table, "newByte", newByte);
    setFunction(state, table, "getByte", getByte);
    setFunction(state, table, "setByte", setByte);
    setFunction(state, table, "deletePtr", deletePointer);
    lua_setfield(state, hostTableIndex, "cvCompat");
}
