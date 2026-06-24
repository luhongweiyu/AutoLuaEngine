#include <jni.h>
#include <android/log.h>
#include <sstream>
#include <utility>

#include "engine/engine.h"
#include "engine/engine_config.h"
#include "platform/android_bridge.h"
#include "runtime/image_store.h"
#include "runtime/log_buffer.h"

extern "C" {
#include "lua.h"
}

namespace {

constexpr const char* kLogTag = "AutoLuaEngine";
Engine gEngine;

void logInfo(const char* message) {
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", message);
    appendLogEntry("info", message == nullptr ? "" : message);
}

void logInfo(const std::string& message) {
    logInfo(message.c_str());
}

void appendJsonString(std::ostringstream& output, const std::string& value) {
    for (char ch : value) {
        switch (ch) {
            case '\\':
                output << "\\\\";
                break;
            case '"':
                output << "\\\"";
                break;
            case '\n':
                output << "\\n";
                break;
            case '\r':
                output << "\\r";
                break;
            case '\t':
                output << "\\t";
                break;
            default:
                output << ch;
                break;
        }
    }
}

std::string imageMetadataJson(const ImageMetadata& metadata) {
    std::ostringstream output;
    output << "{";
    output << "\"id\":" << metadata.id << ",";
    output << "\"type\":\"image\",";
    output << "\"width\":" << metadata.width << ",";
    output << "\"height\":" << metadata.height << ",";
    output << "\"rowStride\":" << metadata.rowStride << ",";
    output << "\"pixelStride\":" << metadata.pixelStride << ",";
    output << "\"byteLength\":" << metadata.byteLength << ",";
    output << "\"format\":\"";
    appendJsonString(output, metadata.format);
    output << "\",";
    output << "\"source\":\"";
    appendJsonString(output, metadata.source);
    output << "\",";
    output << "\"captureDurationMs\":" << metadata.captureDurationMs;
    output << "}";
    return output.str();
}

} // namespace

extern "C" JNIEXPORT void JNICALL
Java_com_autolua_engine_NativeEngine_nativeInit(JNIEnv* env, jclass clazz) {
    (void) clazz;

    // 第一阶段只验证 native 库已经被正确加载。
    // Lua Runtime 会在下一阶段接入，避免一次改动混入太多变量。
    JavaVM* javaVm = nullptr;
    env->GetJavaVM(&javaVm);
    AndroidBridge::init(javaVm);

    gEngine.init();
    logInfo("native engine initialized");
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_autolua_engine_NativeEngine_nativeRunLuaText(JNIEnv* env, jclass clazz, jstring code) {
    (void) clazz;

    logInfo("lua test called");

    const char* codeChars = env->GetStringUTFChars(code, nullptr);
    if (codeChars == nullptr) {
        return env->NewStringUTF("Read Lua code failed");
    }

    std::string result = gEngine.runLuaText(codeChars);
    env->ReleaseStringUTFChars(code, codeChars);

    logInfo("lua result: " + result);
    return env->NewStringUTF(result.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_autolua_engine_NativeEngine_nativeStop(JNIEnv* env, jclass clazz) {
    (void) env;
    (void) clazz;

    gEngine.requestStop();
    logInfo("stop requested");
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_autolua_engine_NativeEngine_nativePause(JNIEnv* env, jclass clazz) {
    (void) env;
    (void) clazz;

    bool accepted = gEngine.requestPause();
    logInfo(accepted ? "pause requested" : "pause ignored: no running script");
    return accepted ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_autolua_engine_NativeEngine_nativeResume(JNIEnv* env, jclass clazz) {
    (void) env;
    (void) clazz;

    bool accepted = gEngine.requestResume();
    logInfo(accepted ? "resume requested" : "resume ignored: script is not paused");
    return accepted ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_autolua_engine_NativeEngine_nativeDrainLogs(JNIEnv* env, jclass clazz, jint afterId) {
    (void) clazz;

    std::vector<LogEntry> entries = drainLogEntries(static_cast<int>(afterId));
    std::ostringstream output;
    output << "[";

    for (size_t i = 0; i < entries.size(); ++i) {
        if (i > 0) {
            output << ",";
        }

        output << "{";
        output << "\"id\":" << entries[i].id << ",";
        output << "\"level\":\"" << entries[i].level << "\",";
        output << "\"message\":\"";

        for (char ch : entries[i].message) {
            switch (ch) {
                case '\\':
                    output << "\\\\";
                    break;
                case '"':
                    output << "\\\"";
                    break;
                case '\n':
                    output << "\\n";
                    break;
                case '\r':
                    output << "\\r";
                    break;
                case '\t':
                    output << "\\t";
                    break;
                default:
                    output << ch;
                    break;
            }
        }

        output << "\"";
        output << "}";
    }

    output << "]";
    return env->NewStringUTF(output.str().c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_autolua_engine_NativeEngine_nativeStatusJson(JNIEnv* env, jclass clazz, jint taskId) {
    (void) clazz;

    std::string status = gEngine.statusJson(static_cast<int>(taskId));
    return env->NewStringUTF(status.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_autolua_engine_NativeEngine_nativeEngineVersion(JNIEnv* env, jclass clazz) {
    (void) clazz;

    return env->NewStringUTF(EngineConfig::kEngineVersion);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_autolua_engine_NativeEngine_nativeLuaVersion(JNIEnv* env, jclass clazz) {
    (void) clazz;

    return env->NewStringUTF(LUA_VERSION);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_autolua_engine_NativeEngine_nativeCaptureScreenJson(JNIEnv* env, jclass clazz) {
    (void) clazz;

    if (!AndroidBridge::hasScreenCapturePermission()) {
        return env->NewStringUTF("{\"ok\":false,\"error\":\"screen capture permission is not granted\"}");
    }

    ScreenCaptureResult capture = AndroidBridge::captureScreen();
    if (!capture.success) {
        std::ostringstream output;
        output << "{\"ok\":false,\"error\":\"";
        appendJsonString(output, capture.error.empty() ? "screen capture failed" : capture.error);
        output << "\"}";
        return env->NewStringUTF(output.str().c_str());
    }

    ImageFrame frame;
    frame.width = capture.width;
    frame.height = capture.height;
    frame.rowStride = capture.rowStride;
    frame.pixelStride = capture.pixelStride;
    frame.format = capture.format;
    frame.source = capture.source;
    frame.captureDurationMs = capture.captureDurationMs;
    frame.pixels = std::move(capture.pixels);

    ImageMetadata metadata = storeImageFrame(std::move(frame));
    std::string metadataJson = imageMetadataJson(metadata);
    std::string result = "{\"ok\":true,\"image\":" + metadataJson + "}";
    return env->NewStringUTF(result.c_str());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_autolua_engine_NativeEngine_nativeReleaseImage(JNIEnv* env, jclass clazz, jint imageId) {
    (void) env;
    (void) clazz;

    return releaseImageFrame(static_cast<int>(imageId)) ? JNI_TRUE : JNI_FALSE;
}
