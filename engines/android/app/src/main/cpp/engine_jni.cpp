/**
 * 文件用途：实现 Java NativeEngine 到 libengine.so 的 JNI 调用入口。
 */
#include <jni.h>
#include <android/log.h>
#include <android/native_window_jni.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "core/api/screen_api.h"
#include "core/api/imgui_api.h"
#include "engine/engine.h"
#include "engine/engine_command.h"
#include "platform/android_bridge.h"
#include "platform/imgui_renderer.h"
#include "runtime/common/log_buffer.h"
#include "runtime/lua/java_bridge.h"

namespace {

constexpr const char* kLogTag = "小鱼精灵";
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
Java_com_xiaoyv_engine_NativeEngine_nativeInit(JNIEnv* env, jclass clazz) {
    (void) clazz;

    // 第一阶段只验证 native 库已经被正确加载。
    // Lua Runtime 会在下一阶段接入，避免一次改动混入太多变量。
    JavaVM* javaVm = nullptr;
    env->GetJavaVM(&javaVm);
    AndroidBridge::init(javaVm);
    initializeLuaJavaBridge(javaVm);

    gEngine.init();
    logInfo("native engine initialized");
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_xiaoyv_engine_NativeEngine_nativeCallJson(JNIEnv* env,
                                                    jclass clazz,
                                                    jstring method,
                                                    jstring paramsJson,
                                                    jstring luaRuntimeBootstrap) {
    (void) clazz;

    // Java/HTTP/Service 只传 method + params；控制命令校验、任务控制和状态查询
    // 都在 libengine.so 内完成，保证 App、IDE 和后续控制端插件复用同一入口。
    std::string result = handleEngineCommand(
            gEngine,
            jStringToString(env, method),
            jStringToString(env, paramsJson),
            jStringToString(env, luaRuntimeBootstrap)
    );
    return env->NewStringUTF(result.c_str());
}

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_xiaoyv_engine_NativeEngine_nativeGetScreenFrame(JNIEnv* env, jclass clazz) {
    (void) clazz;

    // 当前脚本任务内固定屏幕缓冲区不会被刷新或图片切换释放。这里立即复制当前宽高
    // 对应的 RGBA 数据；并发刷新可能改变复制中的像素内容，但不会更换缓冲区地址。
    xiaoyv::api::ScreenFrame frame;
    if (!xiaoyv::api::captureScreen(&frame) || frame.pixels == nullptr) return nullptr;
    constexpr std::size_t kHeaderBytes = 12;
    std::size_t pixelBytes = static_cast<std::size_t>(frame.width)
            * static_cast<std::size_t>(frame.height) * 4U;
    if (pixelBytes > static_cast<std::size_t>(std::numeric_limits<jsize>::max()) - kHeaderBytes) {
        return nullptr;
    }

    std::vector<std::uint8_t> payload(kHeaderBytes + pixelBytes);
    payload[0] = 'X';
    payload[1] = 'Y';
    payload[2] = 'V';
    payload[3] = 'F';
    auto writeInt32 = [&](std::size_t offset, std::uint32_t value) {
        payload[offset] = static_cast<std::uint8_t>(value & 0xFFU);
        payload[offset + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
        payload[offset + 2] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
        payload[offset + 3] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
    };
    writeInt32(4, static_cast<std::uint32_t>(frame.width));
    writeInt32(8, static_cast<std::uint32_t>(frame.height));
    std::memcpy(payload.data() + kHeaderBytes, frame.pixels, pixelBytes);

    jbyteArray result = env->NewByteArray(static_cast<jsize>(payload.size()));
    if (result == nullptr) return nullptr;
    env->SetByteArrayRegion(
            result,
            0,
            static_cast<jsize>(payload.size()),
            reinterpret_cast<const jbyte*>(payload.data())
    );
    return result;
}

/**
 * 将 Java Surface 转为带引用的 ANativeWindow，并启动独立 EGL 渲染线程。
 */
extern "C" JNIEXPORT jboolean JNICALL
Java_com_xiaoyv_engine_NativeEngine_nativeAttachImGuiSurface(
        JNIEnv* env,
        jclass clazz,
        jobject surface
) {
    (void) clazz;
    if (surface == nullptr) {
        return JNI_FALSE;
    }
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (window == nullptr) {
        return JNI_FALSE;
    }
    bool attached = attachImGuiSurface(window);
    ANativeWindow_release(window);
    return attached ? JNI_TRUE : JNI_FALSE;
}

/** SurfaceHolder 销毁时同步停止渲染，确保下一次脚本不会复用旧 EGLContext。 */
extern "C" JNIEXPORT void JNICALL
Java_com_xiaoyv_engine_NativeEngine_nativeDetachImGuiSurface(JNIEnv* env, jclass clazz) {
    (void) env;
    (void) clazz;
    detachImGuiSurface();
}

/** 把 Java WindowManager/Surface 的异步创建错误送入统一 ImGui 生命周期。 */
extern "C" JNIEXPORT void JNICALL
Java_com_xiaoyv_engine_NativeEngine_nativeNotifyImGuiSurfaceFailure(
        JNIEnv* env,
        jclass clazz,
        jstring message
) {
    (void) clazz;
    xiaoyv::api::imguiNotifyRendererFailure(jStringToString(env, message));
}

/** 把 MotionEvent 基础字段复制进 native 输入队列。 */
extern "C" JNIEXPORT void JNICALL
Java_com_xiaoyv_engine_NativeEngine_nativeEnqueueImGuiTouch(
        JNIEnv* env,
        jclass clazz,
        jint action,
        jint pointerId,
        jfloat x,
        jfloat y
) {
    (void) env;
    (void) clazz;
    enqueueImGuiTouch(action, pointerId, x, y);
}

/** 把 Android 输入法提交文本复制进 native 输入队列。 */
extern "C" JNIEXPORT void JNICALL
Java_com_xiaoyv_engine_NativeEngine_nativeEnqueueImGuiText(
        JNIEnv* env,
        jclass clazz,
        jstring text
) {
    (void) clazz;
    std::string utf8 = jStringToString(env, text);
    enqueueImGuiText(utf8.c_str());
}

/** 把 Android KeyEvent 字段复制进 native 输入队列。 */
extern "C" JNIEXPORT void JNICALL
Java_com_xiaoyv_engine_NativeEngine_nativeEnqueueImGuiKey(
        JNIEnv* env,
        jclass clazz,
        jint action,
        jint keyCode,
        jint unicodeCodePoint,
        jint metaState
) {
    (void) env;
    (void) clazz;
    enqueueImGuiKey(action, keyCode, unicodeCodePoint, metaState);
}

/** 把鼠标或触控板滚轮数据复制进 native 输入队列。 */
extern "C" JNIEXPORT void JNICALL
Java_com_xiaoyv_engine_NativeEngine_nativeEnqueueImGuiScroll(
        JNIEnv* env,
        jclass clazz,
        jfloat horizontal,
        jfloat vertical
) {
    (void) env;
    (void) clazz;
    enqueueImGuiScroll(horizontal, vertical);
}
