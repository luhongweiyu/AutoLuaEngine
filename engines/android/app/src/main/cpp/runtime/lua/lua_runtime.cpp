/**
 * 文件用途：实现 Lua 虚拟机生命周期、脚本执行和停止暂停 hook。
 */
#include "lua_runtime.h"

#include "alpkg_package.h"
#include "package/alpkg_crypto.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "../../core/api/runtime_api.h"
#include "host_api.h"
#include "java_bridge.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

LuaRuntime::LuaRuntime()
        : state_(luaL_newstate()),
          shouldInterrupt_(nullptr),
          controlContext_(nullptr),
          package_(nullptr) {
    if (state_ == nullptr) {
        return;
    }

    luaL_openlibs(state_);
    registerHostApi();
    registerLuaJavaBridge(state_, this);
}

LuaRuntime::~LuaRuntime() {
    if (state_ != nullptr) {
        unregisterLuaJavaBridge(state_);
        lua_close(state_);
        state_ = nullptr;
    }
}

std::string LuaRuntime::runText(
        const char* code,
        bool (*shouldInterrupt)(void*),
        void* controlContext) {
    if (state_ == nullptr) {
        return "LuaRuntime init failed";
    }

    package_.reset();
    std::string prepareError = prepareRun(shouldInterrupt, controlContext);
    if (!prepareError.empty()) {
        return prepareError;
    }

    // luaL_loadstring 只负责编译，lua_pcall 才实际执行。
    // 分开处理可以给出更明确的错误来源。
    int loadStatus = luaL_loadstring(state_, code);
    if (loadStatus != LUA_OK) {
        const char* error = lua_tostring(state_, -1);
        std::string message = error == nullptr ? "Lua load error" : error;
        lua_pop(state_, 1);
        return "Lua load failed: " + message;
    }

    return executeLoadedChunk();
}

std::string LuaRuntime::runPackage(
        const std::shared_ptr<AlpkgPackage>& package,
        const char* runtimeBootstrap,
        bool (*shouldInterrupt)(void*),
        void* controlContext) {
    if (state_ == nullptr) {
        return "LuaRuntime init failed";
    }
    if (package == nullptr) {
        return "Lua package is empty";
    }

    package_ = package;
    std::string prepareError = prepareRun(shouldInterrupt, controlContext);
    if (!prepareError.empty()) {
        package_.reset();
        return prepareError;
    }
    installPackageLoaders();

    // 包内入口是预编译字节码，不能像普通脚本一样和 Lua 引导层拼成一段文本。先在同一
    // lua_State 执行运行时引导，再加载包入口，m/lr/cd/import 与普通 .lua 脚本保持一致。
    const char* bootstrap = runtimeBootstrap == nullptr ? "" : runtimeBootstrap;
    int bootstrapStatus = luaL_loadbufferx(
            state_,
            bootstrap,
            std::strlen(bootstrap),
            "@autolua-runtime",
            "t"
    );
    if (bootstrapStatus != LUA_OK) {
        const char* error = lua_tostring(state_, -1);
        std::string message = error == nullptr ? "Lua 运行时引导加载失败" : error;
        lua_pop(state_, 1);
        package_.reset();
        return "Lua load failed: " + message;
    }
    if (lua_pcall(state_, 0, 0, 0) != LUA_OK) {
        const char* error = lua_tostring(state_, -1);
        std::string message = error == nullptr ? "Lua 运行时引导执行失败" : error;
        lua_pop(state_, 1);
        package_.reset();
        return "Lua run failed: " + message;
    }

    std::string loadError;
    std::string chunkName = "@" + package_->entryPath();
    if (loadPackageChunk(state_, package_->entryPath(), chunkName.c_str(), &loadError) != LUA_OK) {
        package_.reset();
        return "Lua load failed: " + loadError;
    }

    std::string result = executeLoadedChunk();
    package_.reset();
    return result;
}

std::string LuaRuntime::prepareRun(bool (*shouldInterrupt)(void*), void* controlContext) {
    shouldInterrupt_ = shouldInterrupt;
    controlContext_ = controlContext;
    lua_pushlightuserdata(state_, this);
    lua_setfield(state_, LUA_REGISTRYINDEX, "AutoLuaEngineRuntime");

    // 每执行一批 VM 指令检查一次控制请求。这个 hook 只用于协作暂停/取消，
    // 不强杀线程，能让 Lua 栈按正常路径等待恢复，或按错误路径展开停止。
    lua_sethook(state_, LuaRuntime::controlHook, LUA_MASKCOUNT, 1000);
    return "";
}

std::string LuaRuntime::executeLoadedChunk() {
    autolua::api::runtimeMarkScriptStart();
    int callStatus = lua_pcall(state_, 0, LUA_MULTRET, 0);
    if (callStatus != LUA_OK) {
        const char* error = lua_tostring(state_, -1);
        std::string message = error == nullptr ? "Lua runtime error" : error;
        lua_pop(state_, 1);
        return "Lua run failed: " + message;
    }
    return "Lua script OK";
}

void LuaRuntime::installPackageLoaders() {
    lua_getglobal(state_, "package");
    lua_getfield(state_, -1, "searchers");
    if (lua_istable(state_, -1)) {
        int count = static_cast<int>(lua_rawlen(state_, -1));
        for (int index = count + 1; index >= 3; --index) {
            lua_rawgeti(state_, -1, index - 1);
            lua_rawseti(state_, -2, index);
        }
        lua_pushlightuserdata(state_, this);
        lua_pushcclosure(state_, LuaRuntime::luaPackageRequireSearcher, 1);
        lua_rawseti(state_, -2, 2);
    }
    lua_pop(state_, 2);

    lua_pushlightuserdata(state_, this);
    lua_pushcclosure(state_, LuaRuntime::luaPackageDofile, 1);
    lua_setglobal(state_, "dofile");
    lua_pushlightuserdata(state_, this);
    lua_pushcclosure(state_, LuaRuntime::luaPackageLoadfile, 1);
    lua_setglobal(state_, "loadfile");
}

int LuaRuntime::loadPackageChunk(
        lua_State* state,
        const std::string& relativePath,
        const char* chunkName,
        std::string* error) {
    LuaRuntime* runtime = nullptr;
    lua_getfield(state, LUA_REGISTRYINDEX, "AutoLuaEngineRuntime");
    runtime = static_cast<LuaRuntime*>(lua_touserdata(state, -1));
    lua_pop(state, 1);
    if (runtime == nullptr || runtime->package_ == nullptr) {
        if (error != nullptr) {
            *error = "脚本包运行时不可用";
        }
        return LUA_ERRFILE;
    }

    std::vector<unsigned char> bytecode;
    std::string loadError;
    if (!runtime->package_->loadLuaFile(relativePath, &bytecode, &loadError)) {
        if (error != nullptr) {
            *error = loadError;
        }
        return LUA_ERRFILE;
    }

    int status = luaL_loadbufferx(
            state,
            reinterpret_cast<const char*>(bytecode.data()),
            bytecode.size(),
            chunkName,
            "b"
    );
    alpkg_wipe(bytecode.data(), bytecode.size());
    if (status != LUA_OK && error != nullptr) {
        const char* luaError = lua_tostring(state, -1);
        *error = luaError == nullptr ? "Lua 字节码加载失败" : luaError;
        lua_pop(state, 1);
    }
    return status;
}

int LuaRuntime::luaPackageRequireSearcher(lua_State* state) {
    LuaRuntime* runtime = static_cast<LuaRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
    const char* moduleName = luaL_checkstring(state, 1);
    if (runtime == nullptr || runtime->package_ == nullptr || moduleName == nullptr) {
        lua_pushliteral(state, "\n\tALPKG 运行时不可用");
        return 1;
    }

    std::string path(moduleName);
    std::replace(path.begin(), path.end(), '.', '/');
    std::string initPath = path + "/init.lua";
    std::string filePath = path + ".lua";
    std::string selectedPath;
    if (runtime->package_->hasLuaFile(filePath)) {
        selectedPath = filePath;
    } else if (runtime->package_->hasLuaFile(initPath)) {
        selectedPath = initPath;
    } else {
        lua_pushfstring(state, "\n\tALPKG 中找不到模块 '%s'", moduleName);
        return 1;
    }

    std::string error;
    std::string chunkName = "@" + selectedPath;
    if (loadPackageChunk(state, selectedPath, chunkName.c_str(), &error) != LUA_OK) {
        return luaL_error(state, "%s", error.c_str());
    }
    return 1;
}

int LuaRuntime::luaPackageDofile(lua_State* state) {
    LuaRuntime* runtime = static_cast<LuaRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
    const char* path = luaL_checkstring(state, 1);
    if (runtime == nullptr || runtime->package_ == nullptr || path == nullptr) {
        return luaL_error(state, "ALPKG 运行时不可用");
    }

    std::string pathText(path);
    lua_settop(state, 0);
    std::string error;
    std::string chunkName = "@" + pathText;
    if (loadPackageChunk(state, pathText, chunkName.c_str(), &error) != LUA_OK) {
        return luaL_error(state, "%s", error.c_str());
    }
    lua_call(state, 0, LUA_MULTRET);
    return lua_gettop(state);
}

int LuaRuntime::luaPackageLoadfile(lua_State* state) {
    LuaRuntime* runtime = static_cast<LuaRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
    const char* path = luaL_checkstring(state, 1);
    if (runtime == nullptr || runtime->package_ == nullptr || path == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "ALPKG 运行时不可用");
        return 2;
    }

    std::string pathText(path);
    lua_settop(state, 0);
    std::string error;
    std::string chunkName = "@" + pathText;
    if (loadPackageChunk(state, pathText, chunkName.c_str(), &error) != LUA_OK) {
        lua_pushnil(state);
        lua_pushstring(state, error.c_str());
        return 2;
    }
    return 1;
}

void LuaRuntime::registerHostApi() {
    ::registerHostApi(state_);
}

bool LuaRuntime::shouldInterruptNow() const {
    return shouldInterrupt_ != nullptr && shouldInterrupt_(controlContext_);
}

std::string LuaRuntime::packagePath() const {
    return package_ == nullptr ? "" : package_->packagePath();
}

void LuaRuntime::controlHook(lua_State* state, lua_Debug* debug) {
    (void) debug;

    // Java 监听器可能在主线程或 Binder 线程触发。所有跨线程回调先排队，
    // 再由脚本所属线程在 hook 中执行，避免并发访问同一个 lua_State。
    processLuaJavaCallbacks(state);

    lua_getfield(state, LUA_REGISTRYINDEX, "AutoLuaEngineRuntime");
    LuaRuntime* runtime = static_cast<LuaRuntime*>(lua_touserdata(state, -1));
    lua_pop(state, 1);

    if (runtime == nullptr) {
        return;
    }

    if (runtime->shouldInterruptNow()) {
        luaL_error(state, "script stopped");
    }
}
