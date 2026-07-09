/**
 * 文件用途：保存 native 引擎版本号等全局配置常量。
 */
#pragma once

/**
 * Native 引擎基础配置。
 *
 * 这里集中放置脚本可见的版本号和包名，避免常量散落在 HostApi、
 * JNI、任务管理等模块里。
 */
namespace EngineConfig {

constexpr const char* kEngineVersion = "0.1.0";
constexpr const char* kAndroidPackageName = "com.autolua.engine";
constexpr const char* kAppFilesDir = "/data/data/com.autolua.engine/files/";

} // namespace EngineConfig
