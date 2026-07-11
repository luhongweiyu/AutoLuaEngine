/**
 * 文件用途：声明 Lua 5.4 与 Android Java 对象之间的动态互操作桥。
 */
#pragma once

#include <jni.h>

struct lua_State;
class LuaRuntime;

/**
 * 保存 JavaVM 并初始化 Java 互操作 JNI 元数据。
 *
 * 必须在 NativeEngine.nativeInit 所在的 Java 线程调用，确保 FindClass 使用应用
 * ClassLoader 找到互操作辅助类。
 */
void initializeLuaJavaBridge(JavaVM* javaVm);

/**
 * 为一个 LuaRuntime 注册 import、Java userdata 元方法和回调上下文。
 */
void registerLuaJavaBridge(lua_State* state, LuaRuntime* runtime);

/**
 * 在 lua_close 前释放 Lua registry 回调引用，并使外部 Java 回调立即失效。
 */
void unregisterLuaJavaBridge(lua_State* state);

/**
 * 在脚本所属线程处理其他 Java 线程排队的 Lua 接口回调。
 *
 * LuaRuntime 的指令 hook 和 sleep 都会调用此函数，保证监听器回调不会并发访问
 * 同一个 lua_State。
 */
void processLuaJavaCallbacks(lua_State* state);
