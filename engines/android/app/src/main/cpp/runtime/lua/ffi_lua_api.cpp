/**
 * 文件用途：为 Lua 5.4 提供 ffi.cdef/ffi.load 的整数、指针 C ABI 调用能力。
 */
#include "ffi_lua_api.h"

#include <dlfcn.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <new>
#include <regex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

namespace {

constexpr const char* kContextRegistryKey = "xiaoyv.ffi.context";
constexpr const char* kContextMetatable = "xiaoyv.FfiContext";
constexpr const char* kLibraryMetatable = "xiaoyv.FfiLibrary";

enum class ValueKind : unsigned char {
    Void = 0,
    Signed32 = 1,
    Unsigned32 = 2,
    Signed64 = 3,
    Unsigned64 = 4,
    Pointer = 5,
    CString = 6,
};

struct Signature {
    ValueKind result = ValueKind::Void;
    std::vector<ValueKind> arguments;
};

struct FfiContext {
    std::unordered_map<std::string, Signature> signatures;
};

struct FfiLibrary {
    void* handle = nullptr;
    bool owned = false;
};

std::string trim(std::string value) {
    auto notSpace = [](unsigned char character) { return !std::isspace(character); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool containsWord(const std::string& text, const std::string& word) {
    return std::regex_search(text, std::regex("\\b" + word + "\\b"));
}

bool parseType(const std::string& declaration, bool result, ValueKind* kind) {
    std::string type = lower(trim(declaration));
    type = std::regex_replace(type, std::regex("\\b(const|volatile|restrict|static|extern)\\b"), "");
    type = trim(std::regex_replace(type, std::regex("\\s+"), " "));
    if (type == "void") {
        if (result) {
            *kind = ValueKind::Void;
            return true;
        }
        return false;
    }
    if (type.find('*') != std::string::npos) {
        *kind = type.find("char") != std::string::npos
                ? ValueKind::CString
                : ValueKind::Pointer;
        return true;
    }
    if (type.find("double") != std::string::npos || type.find("float") != std::string::npos) {
        return false;
    }
    bool unsignedType = containsWord(type, "unsigned")
            || containsWord(type, "uint32_t")
            || containsWord(type, "uint64_t")
            || containsWord(type, "size_t")
            || containsWord(type, "uintptr_t");
    bool fixed64 = type.find("long long") != std::string::npos
            || containsWord(type, "int64_t")
            || containsWord(type, "uint64_t");
    bool pointerSized = containsWord(type, "intptr_t")
            || containsWord(type, "uintptr_t")
            || containsWord(type, "size_t")
            || containsWord(type, "ssize_t");
    if (fixed64 && sizeof(std::uintptr_t) < sizeof(std::uint64_t)) {
        // 当前轻量调用桥按一个 uintptr_t 传递一个参数。32 位 ABI 若把 64 位值塞进
        // 单个槽会截断；没有 libffi 后端时必须在声明阶段明确拒绝，不能伪装成功。
        return false;
    }
    bool wide = fixed64
            || (pointerSized && sizeof(std::uintptr_t) == sizeof(std::uint64_t))
            || (containsWord(type, "long") && sizeof(long) == sizeof(std::uint64_t));
    *kind = wide
            ? (unsignedType ? ValueKind::Unsigned64 : ValueKind::Signed64)
            : (unsignedType ? ValueKind::Unsigned32 : ValueKind::Signed32);
    return true;
}

FfiContext* context(lua_State* state) {
    lua_getfield(state, LUA_REGISTRYINDEX, kContextRegistryKey);
    auto* value = static_cast<FfiContext*>(luaL_checkudata(state, -1, kContextMetatable));
    lua_pop(state, 1);
    return value;
}

int contextGc(lua_State* state) {
    auto* value = static_cast<FfiContext*>(luaL_checkudata(state, 1, kContextMetatable));
    value->~FfiContext();
    return 0;
}

int cdef(lua_State* state) {
    size_t length = 0;
    const char* source = luaL_checklstring(state, 1, &length);
    std::string declarations(source, length);
    declarations = std::regex_replace(
            declarations,
            std::regex(R"(/\*[\s\S]*?\*/|//[^\r\n]*)"),
            ""
    );

    const std::regex prototype(
            R"(([A-Za-z_][A-Za-z0-9_\s\*]*?)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^\)]*)\)\s*;)"
    );
    int parsed = 0;
    for (std::sregex_iterator iterator(declarations.begin(), declarations.end(), prototype), end;
         iterator != end;
         ++iterator) {
        Signature signature;
        if (!parseType((*iterator)[1].str(), true, &signature.result)) {
            return luaL_error(
                    state,
                    "ffi 暂不支持返回类型：%s",
                    trim((*iterator)[1].str()).c_str()
            );
        }
        std::string arguments = trim((*iterator)[3].str());
        if (!arguments.empty() && arguments != "void") {
            size_t start = 0;
            while (start <= arguments.size()) {
                size_t comma = arguments.find(',', start);
                std::string argument = trim(arguments.substr(
                        start,
                        comma == std::string::npos ? std::string::npos : comma - start
                ));
                if (argument == "...") {
                    return luaL_error(state, "ffi 暂不支持可变参数函数");
                }

                // 去掉普通参数名；指针星号仍保留在类型部分。
                argument = std::regex_replace(
                        argument,
                        std::regex(R"(\s+[A-Za-z_][A-Za-z0-9_]*\s*$)"),
                        ""
                );
                ValueKind kind;
                if (!parseType(argument, false, &kind)) {
                    return luaL_error(
                            state,
                            "ffi 暂不支持参数类型：%s",
                            argument.c_str()
                    );
                }
                signature.arguments.push_back(kind);
                if (signature.arguments.size() > 6) {
                    return luaL_error(state, "ffi 单个函数最多支持 6 个参数");
                }
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
        }
        context(state)->signatures[(*iterator)[2].str()] = std::move(signature);
        ++parsed;
    }
    if (parsed == 0 && !trim(declarations).empty()) {
        return luaL_error(state, "ffi.cdef 未找到有效的 C 函数声明");
    }
    return 0;
}

std::uintptr_t invokeInteger(void* address, const std::vector<std::uintptr_t>& values) {
    switch (values.size()) {
        case 0:
            return reinterpret_cast<std::uintptr_t (*)()>(address)();
        case 1:
            return reinterpret_cast<std::uintptr_t (*)(std::uintptr_t)>(address)(values[0]);
        case 2:
            return reinterpret_cast<std::uintptr_t (*)(std::uintptr_t, std::uintptr_t)>(address)(
                    values[0], values[1]
            );
        case 3:
            return reinterpret_cast<std::uintptr_t (*)(
                    std::uintptr_t, std::uintptr_t, std::uintptr_t
            )>(address)(values[0], values[1], values[2]);
        case 4:
            return reinterpret_cast<std::uintptr_t (*)(
                    std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t
            )>(address)(values[0], values[1], values[2], values[3]);
        case 5:
            return reinterpret_cast<std::uintptr_t (*)(
                    std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t,
                    std::uintptr_t
            )>(address)(values[0], values[1], values[2], values[3], values[4]);
        default:
            return reinterpret_cast<std::uintptr_t (*)(
                    std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t,
                    std::uintptr_t, std::uintptr_t
            )>(address)(values[0], values[1], values[2], values[3], values[4], values[5]);
    }
}

void invokeVoid(void* address, const std::vector<std::uintptr_t>& values) {
    switch (values.size()) {
        case 0:
            reinterpret_cast<void (*)()>(address)();
            return;
        case 1:
            reinterpret_cast<void (*)(std::uintptr_t)>(address)(values[0]);
            return;
        case 2:
            reinterpret_cast<void (*)(std::uintptr_t, std::uintptr_t)>(address)(
                    values[0], values[1]
            );
            return;
        case 3:
            reinterpret_cast<void (*)(
                    std::uintptr_t, std::uintptr_t, std::uintptr_t
            )>(address)(values[0], values[1], values[2]);
            return;
        case 4:
            reinterpret_cast<void (*)(
                    std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t
            )>(address)(values[0], values[1], values[2], values[3]);
            return;
        case 5:
            reinterpret_cast<void (*)(
                    std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t,
                    std::uintptr_t
            )>(address)(values[0], values[1], values[2], values[3], values[4]);
            return;
        default:
            reinterpret_cast<void (*)(
                    std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t,
                    std::uintptr_t, std::uintptr_t
            )>(address)(values[0], values[1], values[2], values[3], values[4], values[5]);
    }
}

int callFunction(lua_State* state) {
    void* address = lua_touserdata(state, lua_upvalueindex(1));
    ValueKind resultKind = static_cast<ValueKind>(
            lua_tointeger(state, lua_upvalueindex(2))
    );
    size_t signatureLength = 0;
    const char* encoded = lua_tolstring(state, lua_upvalueindex(3), &signatureLength);
    if (address == nullptr || encoded == nullptr) {
        return luaL_error(state, "ffi 函数已失效");
    }
    if (lua_gettop(state) != static_cast<int>(signatureLength)) {
        return luaL_error(
                state,
                "ffi 函数需要 %d 个参数，实际收到 %d 个",
                static_cast<int>(signatureLength),
                lua_gettop(state)
        );
    }

    std::vector<std::string> strings;
    strings.reserve(signatureLength);
    std::vector<std::uintptr_t> values;
    values.reserve(signatureLength);
    for (size_t index = 0; index < signatureLength; ++index) {
        ValueKind kind = static_cast<ValueKind>(encoded[index]);
        int luaIndex = static_cast<int>(index + 1);
        if (kind == ValueKind::CString) {
            size_t length = 0;
            const char* value = luaL_checklstring(state, luaIndex, &length);
            strings.emplace_back(value, length);
            values.push_back(reinterpret_cast<std::uintptr_t>(strings.back().c_str()));
        } else if (kind == ValueKind::Pointer) {
            if (lua_isnil(state, luaIndex)) {
                values.push_back(0);
            } else if (lua_isuserdata(state, luaIndex) || lua_islightuserdata(state, luaIndex)) {
                values.push_back(reinterpret_cast<std::uintptr_t>(lua_touserdata(state, luaIndex)));
            } else {
                values.push_back(static_cast<std::uintptr_t>(
                        luaL_checkinteger(state, luaIndex)
                ));
            }
        } else {
            values.push_back(static_cast<std::uintptr_t>(
                    luaL_checkinteger(state, luaIndex)
            ));
        }
    }

    if (resultKind == ValueKind::Void) {
        invokeVoid(address, values);
        return 0;
    }
    std::uintptr_t result = invokeInteger(address, values);
    switch (resultKind) {
        case ValueKind::Signed32:
            lua_pushinteger(state, static_cast<lua_Integer>(
                    static_cast<std::int32_t>(result)
            ));
            return 1;
        case ValueKind::Unsigned32:
            lua_pushinteger(state, static_cast<lua_Integer>(
                    static_cast<std::uint32_t>(result)
            ));
            return 1;
        case ValueKind::Signed64:
            lua_pushinteger(state, static_cast<lua_Integer>(
                    static_cast<std::int64_t>(result)
            ));
            return 1;
        case ValueKind::Unsigned64:
        case ValueKind::Pointer:
            lua_pushinteger(state, static_cast<lua_Integer>(result));
            return 1;
        case ValueKind::CString:
            if (result == 0) {
                lua_pushnil(state);
            } else {
                lua_pushstring(state, reinterpret_cast<const char*>(result));
            }
            return 1;
        default:
            return 0;
    }
}

int libraryIndex(lua_State* state) {
    auto* library = static_cast<FfiLibrary*>(luaL_checkudata(state, 1, kLibraryMetatable));
    const char* name = luaL_checkstring(state, 2);
    auto iterator = context(state)->signatures.find(name);
    if (iterator == context(state)->signatures.end()) {
        lua_pushnil(state);
        return 1;
    }
    dlerror();
    void* address = dlsym(library->handle, name);
    const char* error = dlerror();
    if (address == nullptr || error != nullptr) {
        return luaL_error(state, "ffi 找不到符号 %s：%s", name, error == nullptr ? "" : error);
    }

    std::string encoded;
    encoded.reserve(iterator->second.arguments.size());
    for (ValueKind kind : iterator->second.arguments) {
        encoded.push_back(static_cast<char>(kind));
    }
    lua_pushlightuserdata(state, address);
    lua_pushinteger(state, static_cast<lua_Integer>(iterator->second.result));
    lua_pushlstring(state, encoded.data(), encoded.size());
    // 函数闭包持有库 userdata，避免脚本只保存 lib.symbol 后原库先被 GC/dlclose，
    // 留下指向已卸载代码的悬空函数地址。callFunction 无需读取第 4 个 upvalue。
    lua_pushvalue(state, 1);
    lua_pushcclosure(state, callFunction, 4);
    return 1;
}

int libraryGc(lua_State* state) {
    auto* library = static_cast<FfiLibrary*>(luaL_checkudata(state, 1, kLibraryMetatable));
    if (library->owned && library->handle != nullptr) {
        dlclose(library->handle);
    }
    library->handle = nullptr;
    library->owned = false;
    return 0;
}

int load(lua_State* state) {
    const char* input = luaL_checkstring(state, 1);
    std::string name = input == nullptr ? "" : input;
    if (name == "c" || name == "libc") {
        name = "libc.so";
    } else if (name.find('/') == std::string::npos
            && name.find(".so") == std::string::npos) {
        if (name.rfind("lib", 0) != 0) name = "lib" + name;
        name += ".so";
    }

    dlerror();
    void* handle = dlopen(name.c_str(), RTLD_NOW | RTLD_LOCAL);
    const char* error = dlerror();
    if (handle == nullptr) {
        return luaL_error(
                state,
                "ffi.load(%s) 失败：%s",
                name.c_str(),
                error == nullptr ? "未知错误" : error
        );
    }
    void* memory = lua_newuserdatauv(state, sizeof(FfiLibrary), 0);
    auto* library = new(memory) FfiLibrary();
    library->handle = handle;
    library->owned = true;
    luaL_setmetatable(state, kLibraryMetatable);
    return 1;
}

void setFunction(lua_State* state, int tableIndex, const char* name, lua_CFunction function) {
    int absolute = lua_absindex(state, tableIndex);
    lua_pushcfunction(state, function);
    lua_setfield(state, absolute, name);
}

} // namespace

void registerFfiLuaApi(lua_State* state, int hostTableIndex) {
    if (luaL_newmetatable(state, kContextMetatable) != 0) {
        setFunction(state, -1, "__gc", contextGc);
    }
    lua_pop(state, 1);
    if (luaL_newmetatable(state, kLibraryMetatable) != 0) {
        setFunction(state, -1, "__index", libraryIndex);
        setFunction(state, -1, "__gc", libraryGc);
        lua_pushliteral(state, "ffi.library");
        lua_setfield(state, -2, "__name");
    }
    lua_pop(state, 1);

    void* memory = lua_newuserdatauv(state, sizeof(FfiContext), 0);
    new(memory) FfiContext();
    luaL_setmetatable(state, kContextMetatable);
    lua_setfield(state, LUA_REGISTRYINDEX, kContextRegistryKey);

    lua_newtable(state);
    int ffiTable = lua_gettop(state);
    setFunction(state, ffiTable, "cdef", cdef);
    setFunction(state, ffiTable, "load", load);
    lua_setfield(state, hostTableIndex, "ffi");
}
