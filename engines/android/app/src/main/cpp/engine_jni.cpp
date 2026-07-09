#include <jni.h>
#include <android/log.h>

#include "engine/engine.h"
#include "engine/engine_command.h"
#include "core/system_api.h"
#include "runtime/log_buffer.h"

using autolua::core::SystemApi;

namespace {

constexpr const char* kLogTag = "AutoLuaEngine";
Engine gEngine;

void logInfo(const char* message) {
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", message);
    appendLogEntry("info", message == nullptr ? "" : message);
}

std::string jStringToString(JNIEnv* env, jstring value) {
    if (value == nullptr) {
        return "";
    }

    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (chars == nullptr) {
        return "";
    }

    std::string result(chars);
    env->ReleaseStringUTFChars(value, chars);
    return result;
}

} // namespace

extern "C" JNIEXPORT void JNICALL
Java_com_autolua_engine_NativeEngine_nativeInit(JNIEnv* env, jclass clazz) {
    (void) clazz;

    // 第一阶段只验证 native 库已经被正确加载。
    // Lua Runtime 会在下一阶段接入，避免一次改动混入太多变量。
    JavaVM* javaVm = nullptr;
    env->GetJavaVM(&javaVm);
    SystemApi::init(javaVm);

    gEngine.init();
    logInfo("native engine initialized");
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_autolua_engine_NativeEngine_nativeCallJson(JNIEnv* env,
                                                    jclass clazz,
                                                    jstring method,
                                                    jstring paramsJson,
                                                    jstring luaRuntimeBootstrap) {
    (void) clazz;

    // Java/HTTP/Service 只传 method + params；实际命令校验、任务控制和系统 API
    // 调度都在 libengine.so 内完成，保证 IDE、插件和脚本语言后续复用同一入口。
    std::string result = handleEngineCommand(
            gEngine,
            jStringToString(env, method),
            jStringToString(env, paramsJson),
            jStringToString(env, luaRuntimeBootstrap)
    );
    return env->NewStringUTF(result.c_str());
}
