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
#include "lua_task_scheduler.h"

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
    scheduler_ = std::make_unique<LuaTaskScheduler>(state_, this);
    registerHostApi();
    registerLuaJavaBridge(state_, this);
}

LuaRuntime::~LuaRuntime() {
    if (state_ != nullptr) {
        if (scheduler_ != nullptr) {
            scheduler_->stopAndJoinAll();
        }
        unregisterLuaJavaBridge(state_);
        scheduler_.reset();
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

    int registryReference = LUA_NOREF;
    lua_State* executionState = createExecutionState(&registryReference);
    if (executionState == nullptr) {
        return "LuaRuntime main thread init failed";
    }

    // 主脚本不在根状态运行，保证 Java 异步回调始终可以使用空闲根状态。
    int loadStatus = luaL_loadstring(executionState, code == nullptr ? "" : code);
    if (loadStatus != LUA_OK) {
        const char* error = lua_tostring(executionState, -1);
        std::string message = error == nullptr ? "Lua load error" : error;
        lua_pop(executionState, 1);
        luaL_unref(state_, LUA_REGISTRYINDEX, registryReference);
        return "Lua load failed: " + message;
    }

    return executeLoadedChunk(executionState, registryReference);
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

    // 包内入口是预编译字节码。运行时引导只在根状态执行一次并立即返回，用户入口随后
    // 在主任务子状态执行，根状态继续专用于共享全局和 Java 回调。
    const char* bootstrap = runtimeBootstrap == nullptr ? "" : runtimeBootstrap;
    int bootstrapStatus = luaL_loadbufferx(
            state_,
            bootstrap,
            std::strlen(bootstrap),
            "@xiaoyv-runtime",
            "t"
    );
    if (bootstrapStatus != LUA_OK) {
        const char* error = lua_tostring(state_, -1);
        std::string message = error == nullptr ? "Lua 运行时引导加载失败" : error;
        lua_pop(state_, 1);
        package_.reset();
        return "Lua load failed: " + message;
    }
    std::string bootstrapError = scheduler_->runBootstrap();
    if (!bootstrapError.empty()) {
        package_.reset();
        return "Lua run failed: " + bootstrapError;
    }

    int registryReference = LUA_NOREF;
    lua_State* executionState = createExecutionState(&registryReference);
    if (executionState == nullptr) {
        package_.reset();
        return "LuaRuntime main thread init failed";
    }

    std::string loadError;
    std::string chunkName = "@" + package_->entryPath();
    if (loadPackageChunk(
            executionState,
            package_->entryPath(),
            chunkName.c_str(),
            &loadError
    ) != LUA_OK) {
        luaL_unref(state_, LUA_REGISTRYINDEX, registryReference);
        package_.reset();
        return "Lua load failed: " + loadError;
    }

    std::string result = executeLoadedChunk(executionState, registryReference);
    package_.reset();
    return result;
}

std::string LuaRuntime::prepareRun(bool (*shouldInterrupt)(void*), void* controlContext) {
    shouldInterrupt_ = shouldInterrupt;
    controlContext_ = controlContext;
    lua_pushlightuserdata(state_, this);
    lua_setfield(state_, LUA_REGISTRYINDEX, "小鱼精灵Runtime");

    // tickCount 表示整个脚本任务的运行时间，所有 native 子线程共享同一个起点。
    xiaoyv::api::runtimeMarkScriptStart();
    configureTaskState(state_);
    return "";
}

std::string LuaRuntime::executeLoadedChunk(
        lua_State* executionState,
        int registryReference
) {
    return scheduler_->runMain(executionState, registryReference);
}

lua_State* LuaRuntime::createExecutionState(int* registryReference) {
    if (registryReference == nullptr) {
        return nullptr;
    }

    lua_State* executionState = lua_newthread(state_);
    if (executionState == nullptr) {
        lua_pop(state_, 1);
        return nullptr;
    }
    *registryReference = luaL_ref(state_, LUA_REGISTRYINDEX);
    return executionState;
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
    lua_getfield(state, LUA_REGISTRYINDEX, "小鱼精灵Runtime");
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

bool LuaRuntime::shouldInterruptNow(lua_State* state) const {
    if (scheduler_ != nullptr && scheduler_->isTaskStopRequested(state)) {
        return true;
    }
    return shouldInterrupt_ != nullptr && shouldInterrupt_(controlContext_);
}

LuaRuntime* LuaRuntime::fromState(lua_State* state) {
    if (state == nullptr) {
        return nullptr;
    }
    lua_getfield(state, LUA_REGISTRYINDEX, "小鱼精灵Runtime");
    LuaRuntime* runtime = static_cast<LuaRuntime*>(lua_touserdata(state, -1));
    lua_pop(state, 1);
    return runtime;
}

void LuaRuntime::configureTaskState(lua_State* state) {
    if (state == nullptr) {
        return;
    }

    // Hook 同时处理停止、暂停、Java 回调和多任务公平让出。单线程时公平让出只读取
    // 一个原子等待计数，不会在每批指令执行锁操作。
    lua_sethook(state, LuaRuntime::controlHook, LUA_MASKCOUNT, 1000);
}

long long LuaRuntime::startChildThread(
        lua_State* caller,
        int callbackIndex,
        int argumentCount,
        std::string* error
) {
    if (scheduler_ == nullptr) {
        if (error != nullptr) {
            *error = "Lua 多线程运行时不可用";
        }
        return 0;
    }
    return scheduler_->startChild(caller, callbackIndex, argumentCount, error);
}

bool LuaRuntime::stopChildThread(long long taskId, std::string* error) {
    if (scheduler_ == nullptr) {
        if (error != nullptr) {
            *error = "Lua 多线程运行时不可用";
        }
        return false;
    }
    return scheduler_->stopChildAndWait(taskId, error);
}

void LuaRuntime::requestStopAllThreads() {
    if (scheduler_ != nullptr) {
        scheduler_->requestStopAll();
    }
}

bool LuaRuntime::releaseVmForBlocking() {
    return scheduler_ != nullptr && scheduler_->releaseForBlocking();
}

void LuaRuntime::reacquireVmAfterBlocking(bool released) {
    if (scheduler_ != nullptr) {
        scheduler_->reacquireAfterBlocking(released);
    }
}

int LuaRuntime::enterVmFromCallback() {
    return scheduler_ == nullptr ? 0 : scheduler_->enterFromCallback();
}

void LuaRuntime::leaveVmFromCallback(int token) {
    if (scheduler_ != nullptr) {
        scheduler_->leaveFromCallback(token);
    }
}

bool LuaRuntime::isVmOwnedByCurrentThread() const {
    return scheduler_ != nullptr && scheduler_->isVmOwnedByCurrentThread();
}

std::shared_ptr<AlpkgPackage> LuaRuntime::package() const {
    return package_;
}

std::string LuaRuntime::packagePath() const {
    return package_ == nullptr ? "" : package_->packagePath();
}

void LuaRuntime::controlHook(lua_State* state, lua_Debug* debug) {
    (void) debug;

    // Java 监听器可能在主线程或 Binder 线程触发。排队回调由当前持有 VM Gate 的
    // Lua 任务处理，不再依赖创建 LuaRuntime 的固定 OS 线程。
    processLuaJavaCallbacks(state);

    lua_getfield(state, LUA_REGISTRYINDEX, "小鱼精灵Runtime");
    LuaRuntime* runtime = static_cast<LuaRuntime*>(lua_touserdata(state, -1));
    lua_pop(state, 1);

    if (runtime == nullptr) {
        return;
    }

    if (runtime->shouldInterruptNow(state)) {
        luaL_error(state, "script stopped");
        return;
    }

    if (runtime->scheduler_ != nullptr) {
        runtime->scheduler_->yieldIfTaskWaiting();
    }
}
