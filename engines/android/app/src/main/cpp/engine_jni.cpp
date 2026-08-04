/**
 * 文件用途：实现 Java NativeEngine 到 libengine.so 的 JNI 调用入口。
 */
#include <jni.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/bitmap.h>
#include <android/log.h>
#include <android/native_window_jni.h>

#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "core/api/screen_api.h"
#include "core/api/imgui_api.h"
#include "engine/engine.h"
#include "engine/engine_command.h"
#include "platform/android_bridge.h"
#include "platform/imgui_renderer.h"
#include "runtime/common/log_buffer.h"
#include "runtime/lua/java_bridge.h"
#include "runtime/lua/lua_module_source.h"

namespace {

constexpr const char* kLogTag = "小鱼精灵";
constexpr const char* kLuaBootstrapModule = "xiaoyv.runtime.bootstrap";

struct LuaRuntimeAssetSpec {
    const char* moduleName;
    const char* assetPath;
};

constexpr LuaRuntimeAssetSpec kLuaRuntimeAssets[] = {
        {"xiaoyv.runtime.api_m", "runtime/api_m.lua"},
        {"xiaoyv.runtime.compat_extended", "runtime/compat_extended.lua"},
        {"xiaoyv.runtime.compat_lr", "runtime/compat_lr.lua"},
        {"xiaoyv.runtime.compat_cd", "runtime/compat_cd.lua"},
        {"xiaoyv.runtime.yolo", "runtime/yolo.lua"},
        {kLuaBootstrapModule, "runtime/bootstrap.lua"},
        {"ltn12", "runtime/luasocket/ltn12.lua"},
        {"mime", "runtime/luasocket/mime.lua"},
        {"socket", "runtime/luasocket/socket.lua"},
        {"socket.ftp", "runtime/luasocket/socket/ftp.lua"},
        {"socket.headers", "runtime/luasocket/socket/headers.lua"},
        {"socket.http", "runtime/luasocket/socket/http.lua"},
        {"socket.smtp", "runtime/luasocket/socket/smtp.lua"},
        {"socket.tp", "runtime/luasocket/socket/tp.lua"},
        {"socket.url", "runtime/luasocket/socket/url.lua"},
};

Engine& engineInstance() {
    // Engine 会在 nativeInit 中、runtime_api 的静态状态完成构造后才首次创建。
    // 这样进程正常退出时会先析构 Engine，再析构它所使用的 runtime mutex，
    // 避免跨编译单元的静态初始化顺序导致 destroyed mutex 崩溃。
    static Engine instance;
    return instance;
}

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

void throwIllegalState(JNIEnv* env, const std::string& message) {
    jclass exceptionClass = env->FindClass("java/lang/IllegalStateException");
    if (exceptionClass == nullptr) {
        return;
    }
    env->ThrowNew(exceptionClass, message.c_str());
    env->DeleteLocalRef(exceptionClass);
}

bool readAssetSource(
        AAssetManager* assetManager,
        const char* assetPath,
        std::string* source,
        std::string* error) {
    if (assetManager == nullptr || assetPath == nullptr || source == nullptr) {
        if (error != nullptr) *error = "Lua 运行时 AssetManager 或路径为空";
        return false;
    }

    std::unique_ptr<AAsset, void (*)(AAsset*)> asset(
            AAssetManager_open(assetManager, assetPath, AASSET_MODE_STREAMING),
            AAsset_close
    );
    if (asset == nullptr) {
        if (error != nullptr) *error = "打开 Lua 运行时资源失败：" + std::string(assetPath);
        return false;
    }

    off64_t assetSize = AAsset_getLength64(asset.get());
    if (assetSize <= 0
            || static_cast<uint64_t>(assetSize) > std::numeric_limits<std::size_t>::max()) {
        if (error != nullptr) *error = "Lua 运行时资源为空或过大：" + std::string(assetPath);
        return false;
    }

    source->assign(static_cast<std::size_t>(assetSize), '\0');
    std::size_t offset = 0;
    while (offset < source->size()) {
        std::size_t remaining = source->size() - offset;
        std::size_t requestSize = remaining > static_cast<std::size_t>(std::numeric_limits<int>::max())
                ? static_cast<std::size_t>(std::numeric_limits<int>::max())
                : remaining;
        int readCount = AAsset_read(
                asset.get(),
                source->data() + offset,
                requestSize
        );
        if (readCount <= 0) {
            if (error != nullptr) *error = "读取 Lua 运行时资源失败：" + std::string(assetPath);
            return false;
        }
        offset += static_cast<std::size_t>(readCount);
    }
    return true;
}

bool readLuaRuntimeConfig(
        JNIEnv* env,
        jobject assetManagerValue,
        LuaRuntimeConfig* config,
        std::string* error) {
    if (assetManagerValue == nullptr || config == nullptr) {
        if (error != nullptr) *error = "Lua 运行时 AssetManager 为空";
        return false;
    }

    AAssetManager* assetManager = AAssetManager_fromJava(env, assetManagerValue);
    if (assetManager == nullptr) {
        if (error != nullptr) *error = "无法取得 Android AssetManager";
        return false;
    }

    config->modules.clear();
    config->modules.reserve(sizeof(kLuaRuntimeAssets) / sizeof(kLuaRuntimeAssets[0]));
    config->bootstrapModule = kLuaBootstrapModule;
    for (const LuaRuntimeAssetSpec& assetSpec : kLuaRuntimeAssets) {
        std::string source;
        if (!readAssetSource(assetManager, assetSpec.assetPath, &source, error)) {
            return false;
        }
        config->modules.push_back({
                assetSpec.moduleName,
                "@" + std::string(assetSpec.assetPath),
                std::move(source),
        });
    }
    return true;
}

} // namespace

extern "C" JNIEXPORT void JNICALL
Java_com_xiaoyv_engine_NativeEngine_nativeLoadRootSystemLibrary(JNIEnv* env,
                                                                jclass clazz,
                                                                jstring path) {
    (void) clazz;

    // Root Worker 由独立 app_process 启动，没有从 Zygote 继承 Conscrypt 的 JNI 注册。
    // 直接从应用 ClassLoader 调用 System.load 会落入应用 linker namespace，无法访问
    // /system 或 Conscrypt APEX。改由 bootstrap Runtime 类作为调用方执行 load0，确保
    // NativeLoader 使用系统命名空间并正常调用 libjavacrypto 的 JNI_OnLoad。
    jclass runtimeClass = env->FindClass("java/lang/Runtime");
    if (runtimeClass == nullptr) return;

    jmethodID getRuntime = env->GetStaticMethodID(
            runtimeClass,
            "getRuntime",
            "()Ljava/lang/Runtime;"
    );
    if (getRuntime == nullptr) return;

    jobject runtime = env->CallStaticObjectMethod(runtimeClass, getRuntime);
    if (env->ExceptionCheck() || runtime == nullptr) return;

    jmethodID load = env->GetMethodID(
            runtimeClass,
            "load0",
            "(Ljava/lang/Class;Ljava/lang/String;)V"
    );
    if (load != nullptr) {
        env->CallVoidMethod(runtime, load, runtimeClass, path);
    }
    env->DeleteLocalRef(runtime);
    env->DeleteLocalRef(runtimeClass);
}

extern "C" JNIEXPORT void JNICALL
Java_com_xiaoyv_engine_NativeEngine_nativeInit(
        JNIEnv* env,
        jclass clazz,
        jobject assetManager) {
    (void) clazz;

    // Worker 初始化时读取并深拷贝固定 runtime assets；每个脚本创建自己的 Lua VM，
    // 再把这份只读配置注册为 package.preload。
    try {
        LuaRuntimeConfig runtimeConfig;
        std::string configError;
        if (!readLuaRuntimeConfig(
                env,
                assetManager,
                &runtimeConfig,
                &configError
        )) {
            if (!env->ExceptionCheck()) {
                throwIllegalState(env, configError.empty() ? "初始化 Lua 运行时失败" : configError);
            }
            return;
        }

        JavaVM* javaVm = nullptr;
        if (env->GetJavaVM(&javaVm) != JNI_OK || javaVm == nullptr) {
            throwIllegalState(env, "初始化 Lua 运行时失败：无法取得 JavaVM");
            return;
        }
        AndroidBridge::init(javaVm);
        initializeLuaJavaBridge(javaVm);
        engineInstance().init(std::move(runtimeConfig));
    } catch (const std::exception& exception) {
        if (!env->ExceptionCheck()) {
            throwIllegalState(env, std::string("初始化 Lua 运行时失败：") + exception.what());
        }
        return;
    } catch (...) {
        if (!env->ExceptionCheck()) {
            throwIllegalState(env, "初始化 Lua 运行时失败：native 异常");
        }
        return;
    }
    logInfo("native engine initialized");
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_xiaoyv_engine_NativeEngine_nativeCallJson(JNIEnv* env,
                                                    jclass clazz,
                                                    jstring method,
                                                    jstring paramsJson) {
    (void) clazz;

    // Java/HTTP/Service 只传 method + params；控制命令校验、任务控制和状态查询
    // 都在 libengine.so 内完成，保证 App、IDE 和后续控制端插件复用同一入口。
    std::string result = handleEngineCommand(
            engineInstance(),
            jStringToString(env, method),
            jStringToString(env, paramsJson)
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

extern "C" JNIEXPORT jboolean JNICALL
Java_com_xiaoyv_engine_NativeEngine_nativeSetScreenBitmap(
        JNIEnv* env,
        jclass clazz,
        jobject bitmap,
        jint screenWidth,
        jint screenHeight
) {
    (void) clazz;
    if (bitmap == nullptr) {
        xiaoyv::api::restoreScreenPixelOverride();
        return JNI_TRUE;
    }

    AndroidBitmapInfo info{};
    if (AndroidBitmap_getInfo(env, bitmap, &info) != ANDROID_BITMAP_RESULT_SUCCESS
            || info.width == 0
            || info.height == 0
            || info.format != ANDROID_BITMAP_FORMAT_RGBA_8888
            || info.stride < info.width * 4U) {
        return JNI_FALSE;
    }

    std::size_t rowBytes = static_cast<std::size_t>(info.width) * 4U;
    std::size_t pixelBytes = rowBytes * static_cast<std::size_t>(info.height);
    if (pixelBytes > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return JNI_FALSE;
    }

    std::vector<unsigned char> pixels;
    try {
        pixels.resize(pixelBytes);
    } catch (const std::bad_alloc&) {
        return JNI_FALSE;
    }
    void* sourcePixels = nullptr;
    if (AndroidBitmap_lockPixels(env, bitmap, &sourcePixels) != ANDROID_BITMAP_RESULT_SUCCESS
            || sourcePixels == nullptr) {
        return JNI_FALSE;
    }

    const auto* source = static_cast<const unsigned char*>(sourcePixels);
    for (std::size_t row = 0; row < static_cast<std::size_t>(info.height); ++row) {
        std::memcpy(
                pixels.data() + row * rowBytes,
                source + row * static_cast<std::size_t>(info.stride),
                rowBytes
        );
    }
    AndroidBitmap_unlockPixels(env, bitmap);

    return xiaoyv::api::setScreenPixelOverride(
            std::move(pixels),
            static_cast<int>(info.width),
            static_cast<int>(info.height),
            static_cast<int>(screenWidth),
            static_cast<int>(screenHeight)
    ) ? JNI_TRUE : JNI_FALSE;
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
