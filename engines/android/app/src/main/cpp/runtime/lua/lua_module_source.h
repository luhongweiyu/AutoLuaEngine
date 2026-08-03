/**
 * 文件用途：描述由 Android assets 提供、注册到 package.preload 的 Lua 模块源码。
 */
#pragma once

#include <string>
#include <vector>

struct LuaModuleSource {
    std::string name;
    std::string chunkName;
    std::string source;
};

struct LuaRuntimeConfig {
    std::vector<LuaModuleSource> modules;
    std::string bootstrapModule;
};
