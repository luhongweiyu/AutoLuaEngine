/**
 * 文件用途：实现可选 libxiaoyv_yolo.so 的 Java/JNI 边界。
 *
 * 所有方法统一返回 {"ok":boolean,"data":...} JSON 信封。Java 只负责可选 SO 的动态加载、
 * 参数路径规范化和固定操作分发；Lua、JS、Go 仍通过 libengine.so 的 C ABI 进入这里。
 */
#include <jni.h>

#include <limits>
#include <string>

#include "yolo_runtime.h"

namespace {

std::string toString(JNIEnv* env, jstring value) {
    if (env == nullptr || value == nullptr) {
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

void appendJsonString(std::string* output, const std::string& value) {
    if (output == nullptr) {
        return;
    }
    output->push_back('"');
    constexpr char kHex[] = "0123456789abcdef";
    for (unsigned char character : value) {
        switch (character) {
            case '"': output->append("\\\""); break;
            case '\\': output->append("\\\\"); break;
            case '\b': output->append("\\b"); break;
            case '\f': output->append("\\f"); break;
            case '\n': output->append("\\n"); break;
            case '\r': output->append("\\r"); break;
            case '\t': output->append("\\t"); break;
            default:
                if (character < 0x20U) {
                    output->append("\\u00");
                    output->push_back(kHex[(character >> 4U) & 0x0FU]);
                    output->push_back(kHex[character & 0x0FU]);
                } else {
                    output->push_back(static_cast<char>(character));
                }
                break;
        }
    }
    output->push_back('"');
}

std::string success(const std::string& dataJson) {
    return "{\"ok\":true,\"data\":" + dataJson + "}";
}

std::string failure(const std::string& error) {
    std::string result = "{\"ok\":false,\"error\":";
    appendJsonString(&result, error.empty() ? "YOLO 原生调用失败" : error);
    result.push_back('}');
    return result;
}

jstring toJavaString(JNIEnv* env, const std::string& value) {
    return env == nullptr ? nullptr : env->NewStringUTF(value.c_str());
}

bool checkedRgbaLength(int width, int height, size_t* byteCount) {
    if (byteCount == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    const size_t nativeWidth = static_cast<size_t>(width);
    const size_t nativeHeight = static_cast<size_t>(height);
    if (nativeWidth > std::numeric_limits<size_t>::max() / nativeHeight
            || nativeWidth * nativeHeight > std::numeric_limits<size_t>::max() / 4U) {
        return false;
    }
    *byteCount = nativeWidth * nativeHeight * 4U;
    return true;
}

} // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_xiaoyv_engine_YoloPlatformBridge_nativeLoad(
        JNIEnv* env,
        jclass,
        jstring name,
        jstring labelsPath,
        jstring paramPath,
        jstring binPath,
        jstring inputBlob,
        jstring output8Blob,
        jstring output16Blob,
        jstring output32Blob,
        jboolean useGpu
) {
    xiaoyv::yolo::ModelSpec spec;
    spec.name = toString(env, name);
    spec.labelsPath = toString(env, labelsPath);
    spec.paramPath = toString(env, paramPath);
    spec.binPath = toString(env, binPath);
    spec.inputBlob = toString(env, inputBlob);
    spec.output8Blob = toString(env, output8Blob);
    spec.output16Blob = toString(env, output16Blob);
    spec.output32Blob = toString(env, output32Blob);
    spec.useGpu = useGpu == JNI_TRUE;

    std::string error;
    if (!xiaoyv::yolo::loadModel(spec, &error)) {
        return toJavaString(env, failure(error));
    }

    std::string data = "{\"name\":";
    appendJsonString(&data, spec.name);
    data.append(",\"loaded\":true}");
    return toJavaString(env, success(data));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_xiaoyv_engine_YoloPlatformBridge_nativeRelease(
        JNIEnv* env,
        jclass,
        jstring name
) {
    const std::string modelName = toString(env, name);
    bool released = false;
    std::string error;
    if (!xiaoyv::yolo::releaseModel(modelName, &released, &error)) {
        return toJavaString(env, failure(error));
    }

    std::string data = "{\"name\":";
    appendJsonString(&data, modelName);
    data.append(released ? ",\"released\":true}" : ",\"released\":false}");
    return toJavaString(env, success(data));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_xiaoyv_engine_YoloPlatformBridge_nativeIsLoaded(
        JNIEnv* env,
        jclass,
        jstring name
) {
    const std::string modelName = toString(env, name);
    bool loaded = false;
    std::string error;
    if (!xiaoyv::yolo::isModelLoaded(modelName, &loaded, &error)) {
        return toJavaString(env, failure(error));
    }

    std::string data = "{\"name\":";
    appendJsonString(&data, modelName);
    data.append(loaded ? ",\"loaded\":true}" : ",\"loaded\":false}");
    return toJavaString(env, success(data));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_xiaoyv_engine_YoloPlatformBridge_nativeDetectRgba(
        JNIEnv* env,
        jclass,
        jstring name,
        jobject pixels,
        jint width,
        jint height,
        jint left,
        jint top,
        jint right,
        jint bottom,
        jint targetSize,
        jint threads,
        jfloat probabilityThreshold,
        jfloat nmsThreshold
) {
    size_t expectedBytes = 0;
    if (pixels == nullptr || !checkedRgbaLength(width, height, &expectedBytes)) {
        return toJavaString(env, failure("YOLO RGBA 输入尺寸无效"));
    }
    void* data = env->GetDirectBufferAddress(pixels);
    const jlong capacity = env->GetDirectBufferCapacity(pixels);
    if (data == nullptr || capacity < 0 || static_cast<unsigned long long>(capacity) < expectedBytes) {
        return toJavaString(env, failure("YOLO RGBA 输入必须是完整的直接缓冲区"));
    }

    xiaoyv::yolo::DetectOptions options;
    options.targetSize = targetSize;
    options.threads = threads;
    options.probabilityThreshold = probabilityThreshold;
    options.nmsThreshold = nmsThreshold;

    std::string result;
    std::string error;
    if (!xiaoyv::yolo::detectRgba(
                toString(env, name),
                static_cast<const unsigned char*>(data),
                expectedBytes,
                width,
                height,
                left,
                top,
                right,
                bottom,
                options,
                &result,
                &error)) {
        return toJavaString(env, failure(error));
    }
    return toJavaString(env, success(result));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_xiaoyv_engine_YoloPlatformBridge_nativeRuntimeInfo(JNIEnv* env, jclass) {
    return toJavaString(env, success("{\"available\":true," + xiaoyv::yolo::runtimeInfoJson().substr(1)));
}
