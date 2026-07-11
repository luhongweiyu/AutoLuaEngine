/**
 * 文件用途：实现 libengine.so 到 AndroidHostBridge 的最小 JNI 调用桥。
 */
#include "android_bridge.h"

#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>

namespace {

JavaVM* gJavaVm = nullptr;
jclass gBridgeClass = nullptr;
std::mutex gBridgeMutex;

JNIEnv* getEnv() {
    if (gJavaVm == nullptr) {
        return nullptr;
    }

    JNIEnv* env = nullptr;
    jint result = gJavaVm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (result == JNI_OK) {
        return env;
    }

    if (result == JNI_EDETACHED && gJavaVm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
        return env;
    }

    return nullptr;
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

jclass bridgeClass(JNIEnv* env) {
    if (env == nullptr || gBridgeClass == nullptr) {
        return nullptr;
    }
    return gBridgeClass;
}

bool clearExceptionIfNeeded(JNIEnv* env) {
    if (env != nullptr && env->ExceptionCheck()) {
        env->ExceptionClear();
        return true;
    }
    return false;
}

jmethodID staticMethod(JNIEnv* env, const char* methodName, const char* signature) {
    jclass clazz = bridgeClass(env);
    if (clazz == nullptr) {
        return nullptr;
    }

    jmethodID methodId = env->GetStaticMethodID(clazz, methodName, signature);
    if (methodId == nullptr) {
        clearExceptionIfNeeded(env);
    }
    return methodId;
}

bool callBoolean0(const char* methodName) {
    JNIEnv* env = getEnv();
    jmethodID methodId = staticMethod(env, methodName, "()Z");
    if (env == nullptr || methodId == nullptr) {
        return false;
    }

    jboolean result = env->CallStaticBooleanMethod(gBridgeClass, methodId);
    if (clearExceptionIfNeeded(env)) {
        return false;
    }
    return result == JNI_TRUE;
}

bool callBoolean1(const char* methodName, bool value) {
    JNIEnv* env = getEnv();
    jmethodID methodId = staticMethod(env, methodName, "(Z)Z");
    if (env == nullptr || methodId == nullptr) {
        return false;
    }

    jboolean result = env->CallStaticBooleanMethod(
            gBridgeClass,
            methodId,
            value ? JNI_TRUE : JNI_FALSE
    );
    if (clearExceptionIfNeeded(env)) {
        return false;
    }
    return result == JNI_TRUE;
}

bool callBoolean1Int(const char* methodName, int value) {
    JNIEnv* env = getEnv();
    jmethodID methodId = staticMethod(env, methodName, "(I)Z");
    if (env == nullptr || methodId == nullptr) {
        return false;
    }

    jboolean result = env->CallStaticBooleanMethod(gBridgeClass, methodId, static_cast<jint>(value));
    if (clearExceptionIfNeeded(env)) {
        return false;
    }
    return result == JNI_TRUE;
}

bool callBoolean3Int(const char* methodName, int first, int second, int third) {
    JNIEnv* env = getEnv();
    jmethodID methodId = staticMethod(env, methodName, "(III)Z");
    if (env == nullptr || methodId == nullptr) {
        return false;
    }

    jboolean result = env->CallStaticBooleanMethod(
            gBridgeClass,
            methodId,
            static_cast<jint>(first),
            static_cast<jint>(second),
            static_cast<jint>(third)
    );
    if (clearExceptionIfNeeded(env)) {
        return false;
    }
    return result == JNI_TRUE;
}

bool callBooleanString(const char* methodName, const std::string& text) {
    JNIEnv* env = getEnv();
    jmethodID methodId = staticMethod(env, methodName, "(Ljava/lang/String;)Z");
    if (env == nullptr || methodId == nullptr) {
        return false;
    }

    jstring value = env->NewStringUTF(text.c_str());
    if (clearExceptionIfNeeded(env) || value == nullptr) {
        return false;
    }

    jboolean result = env->CallStaticBooleanMethod(gBridgeClass, methodId, value);
    env->DeleteLocalRef(value);
    if (clearExceptionIfNeeded(env)) {
        return false;
    }
    return result == JNI_TRUE;
}

int callInt0(const char* methodName) {
    JNIEnv* env = getEnv();
    jmethodID methodId = staticMethod(env, methodName, "()I");
    if (env == nullptr || methodId == nullptr) {
        return 0;
    }

    jint result = env->CallStaticIntMethod(gBridgeClass, methodId);
    if (clearExceptionIfNeeded(env)) {
        return 0;
    }
    return static_cast<int>(result);
}

std::string callString0(const char* methodName) {
    JNIEnv* env = getEnv();
    jmethodID methodId = staticMethod(env, methodName, "()Ljava/lang/String;");
    if (env == nullptr || methodId == nullptr) {
        return "";
    }

    jstring value = static_cast<jstring>(env->CallStaticObjectMethod(gBridgeClass, methodId));
    if (clearExceptionIfNeeded(env)) {
        return "";
    }

    std::string result = jStringToString(env, value);
    if (value != nullptr) {
        env->DeleteLocalRef(value);
    }
    return result;
}

ScreenCaptureResult captureFailure(const std::string& error) {
    ScreenCaptureResult result;
    result.success = false;
    result.error = error;
    return result;
}

RootStatusResult rootStatusFailure(const std::string& error) {
    RootStatusResult result;
    result.available = false;
    result.error = error;
    return result;
}

RootProbeAttempt readProbeAttempt(JNIEnv* env, jobject attemptObject) {
    RootProbeAttempt attempt;
    if (env == nullptr || attemptObject == nullptr) {
        attempt.error = "root probe attempt is empty";
        return attempt;
    }

    jclass attemptClass = env->GetObjectClass(attemptObject);
    if (attemptClass == nullptr) {
        clearExceptionIfNeeded(env);
        attempt.error = "root probe attempt class is not available";
        return attempt;
    }

    jfieldID commandModeField = env->GetFieldID(attemptClass, "commandMode", "Ljava/lang/String;");
    jfieldID suPathField = env->GetFieldID(attemptClass, "suPath", "Ljava/lang/String;");
    jfieldID exitCodeField = env->GetFieldID(attemptClass, "exitCode", "I");
    jfieldID stdoutField = env->GetFieldID(attemptClass, "stdout", "Ljava/lang/String;");
    jfieldID stderrField = env->GetFieldID(attemptClass, "stderr", "Ljava/lang/String;");
    jfieldID timedOutField = env->GetFieldID(attemptClass, "timedOut", "Z");
    jfieldID errorField = env->GetFieldID(attemptClass, "error", "Ljava/lang/String;");
    if (commandModeField == nullptr
            || suPathField == nullptr
            || exitCodeField == nullptr
            || stdoutField == nullptr
            || stderrField == nullptr
            || timedOutField == nullptr
            || errorField == nullptr) {
        clearExceptionIfNeeded(env);
        env->DeleteLocalRef(attemptClass);
        attempt.error = "root probe attempt fields are not available";
        return attempt;
    }

    jstring commandMode = static_cast<jstring>(env->GetObjectField(attemptObject, commandModeField));
    jstring suPath = static_cast<jstring>(env->GetObjectField(attemptObject, suPathField));
    jstring stdoutText = static_cast<jstring>(env->GetObjectField(attemptObject, stdoutField));
    jstring stderrText = static_cast<jstring>(env->GetObjectField(attemptObject, stderrField));
    jstring errorText = static_cast<jstring>(env->GetObjectField(attemptObject, errorField));

    attempt.commandMode = jStringToString(env, commandMode);
    attempt.suPath = jStringToString(env, suPath);
    attempt.exitCode = env->GetIntField(attemptObject, exitCodeField);
    attempt.stdoutText = jStringToString(env, stdoutText);
    attempt.stderrText = jStringToString(env, stderrText);
    attempt.timedOut = env->GetBooleanField(attemptObject, timedOutField) == JNI_TRUE;
    attempt.error = jStringToString(env, errorText);

    if (commandMode != nullptr) {
        env->DeleteLocalRef(commandMode);
    }
    if (suPath != nullptr) {
        env->DeleteLocalRef(suPath);
    }
    if (stdoutText != nullptr) {
        env->DeleteLocalRef(stdoutText);
    }
    if (stderrText != nullptr) {
        env->DeleteLocalRef(stderrText);
    }
    if (errorText != nullptr) {
        env->DeleteLocalRef(errorText);
    }
    env->DeleteLocalRef(attemptClass);
    return attempt;
}

void readProbeAttempts(JNIEnv* env, jobject attemptsObject, RootStatusResult* status) {
    if (env == nullptr || attemptsObject == nullptr || status == nullptr) {
        return;
    }

    jclass listClass = env->FindClass("java/util/List");
    if (listClass == nullptr) {
        clearExceptionIfNeeded(env);
        return;
    }

    jmethodID sizeMethod = env->GetMethodID(listClass, "size", "()I");
    jmethodID getMethod = env->GetMethodID(listClass, "get", "(I)Ljava/lang/Object;");
    if (sizeMethod == nullptr || getMethod == nullptr) {
        clearExceptionIfNeeded(env);
        env->DeleteLocalRef(listClass);
        return;
    }

    jint size = env->CallIntMethod(attemptsObject, sizeMethod);
    if (clearExceptionIfNeeded(env)) {
        env->DeleteLocalRef(listClass);
        return;
    }

    for (jint index = 0; index < size; ++index) {
        jobject item = env->CallObjectMethod(attemptsObject, getMethod, index);
        if (clearExceptionIfNeeded(env)) {
            break;
        }
        if (item != nullptr) {
            status->attempts.push_back(readProbeAttempt(env, item));
            env->DeleteLocalRef(item);
        }
    }

    env->DeleteLocalRef(listClass);
}

ScreenCaptureResult readScreenCaptureResult(
        JNIEnv* env,
        jobject resultObject,
        unsigned char** targetPixels,
        size_t* targetCapacity
) {
    if (env == nullptr || resultObject == nullptr) {
        return captureFailure("root capture returned empty result");
    }

    jclass resultClass = env->GetObjectClass(resultObject);
    if (resultClass == nullptr) {
        clearExceptionIfNeeded(env);
        return captureFailure("root capture result class is not available");
    }

    jfieldID successField = env->GetFieldID(resultClass, "success", "Z");
    jfieldID pixelsField = env->GetFieldID(resultClass, "pixels", "[B");
    jfieldID pixelBytesField = env->GetFieldID(resultClass, "pixelBytes", "I");
    jfieldID widthField = env->GetFieldID(resultClass, "width", "I");
    jfieldID heightField = env->GetFieldID(resultClass, "height", "I");
    jfieldID rowStrideField = env->GetFieldID(resultClass, "rowStride", "I");
    jfieldID pixelStrideField = env->GetFieldID(resultClass, "pixelStride", "I");
    jfieldID sourceField = env->GetFieldID(resultClass, "source", "Ljava/lang/String;");
    jfieldID durationField = env->GetFieldID(resultClass, "captureDurationMs", "J");
    jfieldID errorField = env->GetFieldID(resultClass, "error", "Ljava/lang/String;");
    jmethodID closeMethod = env->GetMethodID(resultClass, "close", "()V");
    if (successField == nullptr
            || pixelsField == nullptr
            || pixelBytesField == nullptr
            || widthField == nullptr
            || heightField == nullptr
            || rowStrideField == nullptr
            || pixelStrideField == nullptr
            || sourceField == nullptr
            || durationField == nullptr
            || errorField == nullptr
            || closeMethod == nullptr) {
        clearExceptionIfNeeded(env);
        env->DeleteLocalRef(resultClass);
        return captureFailure("root capture result fields are not available");
    }

    ScreenCaptureResult result;
    result.success = env->GetBooleanField(resultObject, successField) == JNI_TRUE;
    result.pixelBytes = static_cast<size_t>(env->GetIntField(resultObject, pixelBytesField));
    result.width = env->GetIntField(resultObject, widthField);
    result.height = env->GetIntField(resultObject, heightField);
    result.rowStride = env->GetIntField(resultObject, rowStrideField);
    result.pixelStride = env->GetIntField(resultObject, pixelStrideField);
    result.captureDurationMs = static_cast<long long>(env->GetLongField(resultObject, durationField));

    jbyteArray pixelsArray = static_cast<jbyteArray>(env->GetObjectField(resultObject, pixelsField));
    jstring source = static_cast<jstring>(env->GetObjectField(resultObject, sourceField));
    jstring error = static_cast<jstring>(env->GetObjectField(resultObject, errorField));
    result.source = jStringToString(env, source);
    result.error = jStringToString(env, error);

    if (result.success) {
        if (result.width <= 0 || result.height <= 0) {
            result.success = false;
            result.error = "root capture size is invalid";
        } else if (result.pixelStride < 4) {
            result.success = false;
            result.error = "root capture pixel stride is unsupported";
        } else if (targetPixels == nullptr || targetCapacity == nullptr) {
            result.success = false;
            result.error = "root capture target buffer is null";
        } else {
            int compactRowStride = result.width * 4;
            size_t targetSize = static_cast<size_t>(compactRowStride)
                    * static_cast<size_t>(result.height);
            size_t requiredSourceSize = static_cast<size_t>(result.rowStride)
                    * static_cast<size_t>(result.height - 1)
                    + static_cast<size_t>(result.width) * static_cast<size_t>(result.pixelStride);
            jsize sourceLength = pixelsArray == nullptr ? 0 : env->GetArrayLength(pixelsArray);

            if (pixelsArray == nullptr) {
                if (*targetPixels == nullptr || *targetCapacity < targetSize) {
                    result.success = false;
                    result.error = "root capture native cache is smaller than returned frame";
                } else if (result.pixelBytes < targetSize) {
                    result.success = false;
                    result.error = "root capture native buffer is incomplete";
                } else {
                    result.pixelBytes = targetSize;
                    result.rowStride = compactRowStride;
                    result.pixelStride = 4;
                }
            } else if (sourceLength <= 0) {
                result.success = false;
                result.error = "root capture pixel array is empty";
            } else if (static_cast<size_t>(sourceLength) < requiredSourceSize) {
                result.success = false;
                result.error = "root capture pixel buffer is incomplete";
            } else if (targetSize > static_cast<size_t>(std::numeric_limits<jsize>::max())) {
                result.success = false;
                result.error = "root capture pixel buffer is too large for JNI copy";
            } else {
                if (*targetPixels == nullptr || *targetCapacity < targetSize) {
                    void* newBuffer = std::realloc(*targetPixels, targetSize);
                    if (newBuffer == nullptr) {
                        result.success = false;
                        result.error = "root capture native cache memory is not enough";
                    } else {
                        *targetPixels = static_cast<unsigned char*>(newBuffer);
                        *targetCapacity = targetSize;
                    }
                }

                if (!result.success) {
                    // 上面的扩容已经写入错误。
                } else if (result.pixelStride == 4 && result.rowStride == compactRowStride) {
                    env->GetByteArrayRegion(
                            pixelsArray,
                            0,
                            static_cast<jsize>(targetSize),
                            reinterpret_cast<jbyte*>(*targetPixels)
                    );
                    result.pixelBytes = targetSize;
                } else {
                    jbyte* sourceBytes = env->GetByteArrayElements(pixelsArray, nullptr);
                    if (sourceBytes == nullptr) {
                        result.success = false;
                        result.error = "root capture pixel array is not available";
                    } else {
                        const auto* source = reinterpret_cast<const unsigned char*>(sourceBytes);
                        for (int y = 0; y < result.height; ++y) {
                            const unsigned char* sourceRow = source
                                    + static_cast<size_t>(y) * static_cast<size_t>(result.rowStride);
                            unsigned char* targetRow = *targetPixels
                                    + static_cast<size_t>(y) * static_cast<size_t>(compactRowStride);

                            if (result.pixelStride == 4) {
                                std::memcpy(targetRow, sourceRow, static_cast<size_t>(compactRowStride));
                                continue;
                            }

                            for (int x = 0; x < result.width; ++x) {
                                const unsigned char* sourcePixel = sourceRow
                                        + static_cast<size_t>(x) * static_cast<size_t>(result.pixelStride);
                                unsigned char* targetPixel = targetRow + static_cast<size_t>(x) * 4;
                                targetPixel[0] = sourcePixel[0];
                                targetPixel[1] = sourcePixel[1];
                                targetPixel[2] = sourcePixel[2];
                                targetPixel[3] = sourcePixel[3];
                            }
                        }
                        env->ReleaseByteArrayElements(pixelsArray, sourceBytes, JNI_ABORT);
                        result.pixelBytes = targetSize;
                    }
                }

                result.rowStride = compactRowStride;
                result.pixelStride = 4;
            }
        }
    } else if (result.error.empty()) {
        result.error = "root capture failed";
    }

    env->CallVoidMethod(resultObject, closeMethod);
    clearExceptionIfNeeded(env);

    if (pixelsArray != nullptr) {
        env->DeleteLocalRef(pixelsArray);
    }
    if (source != nullptr) {
        env->DeleteLocalRef(source);
    }
    if (error != nullptr) {
        env->DeleteLocalRef(error);
    }
    env->DeleteLocalRef(resultClass);
    return result;
}

} // namespace

void AndroidBridge::init(JavaVM* javaVm) {
    std::lock_guard<std::mutex> lock(gBridgeMutex);
    gJavaVm = javaVm;

    JNIEnv* env = getEnv();
    if (env == nullptr) {
        return;
    }

    if (gBridgeClass != nullptr) {
        env->DeleteGlobalRef(gBridgeClass);
        gBridgeClass = nullptr;
    }

    jclass localClass = env->FindClass("com/autolua/engine/AndroidHostBridge");
    if (localClass == nullptr) {
        clearExceptionIfNeeded(env);
        return;
    }

    gBridgeClass = static_cast<jclass>(env->NewGlobalRef(localClass));
    env->DeleteLocalRef(localClass);
}

bool AndroidBridge::isAccessibilityEnabled() {
    return callBoolean0("isAccessibilityEnabled");
}

int AndroidBridge::apiLevel() {
    return callInt0("apiLevel");
}

int AndroidBridge::httpPort() {
    return callInt0("httpPort");
}

std::string AndroidBridge::packageName() {
    return callString0("packageName");
}

bool AndroidBridge::isRootModeEnabled() {
    return callBoolean0("isRootModeEnabled");
}

bool AndroidBridge::setRootModeEnabled(bool enabled) {
    return callBoolean1("setRootModeEnabled", enabled);
}

bool AndroidBridge::isRootAvailable() {
    return callBoolean0("isRootAvailable");
}

bool AndroidBridge::isRootRuntimeReady() {
    return callBoolean0("isRootRuntimeReady");
}

bool AndroidBridge::prepareRootRuntime() {
    return callBoolean0("prepareRootRuntime");
}

bool AndroidBridge::prepareRootHelper() {
    return callBoolean0("prepareRootHelper");
}

RootStatusResult AndroidBridge::rootStatus() {
    JNIEnv* env = getEnv();
    jmethodID methodId = staticMethod(env, "rootStatus", "()Lcom/autolua/engine/RootStatus;");
    if (env == nullptr || methodId == nullptr) {
        return rootStatusFailure("root status method is not available");
    }

    jobject statusObject = env->CallStaticObjectMethod(gBridgeClass, methodId);
    if (clearExceptionIfNeeded(env) || statusObject == nullptr) {
        return rootStatusFailure("root status java call failed");
    }

    jclass statusClass = env->GetObjectClass(statusObject);
    if (statusClass == nullptr) {
        clearExceptionIfNeeded(env);
        env->DeleteLocalRef(statusObject);
        return rootStatusFailure("root status class is not available");
    }

    jfieldID availableField = env->GetFieldID(statusClass, "available", "Z");
    jfieldID commandModeField = env->GetFieldID(statusClass, "commandMode", "Ljava/lang/String;");
    jfieldID suPathField = env->GetFieldID(statusClass, "suPath", "Ljava/lang/String;");
    jfieldID cachedField = env->GetFieldID(statusClass, "cached", "Z");
    jfieldID cacheExpireAtField = env->GetFieldID(statusClass, "cacheExpireAt", "J");
    jfieldID errorField = env->GetFieldID(statusClass, "error", "Ljava/lang/String;");
    jfieldID attemptsField = env->GetFieldID(statusClass, "attempts", "Ljava/util/List;");
    if (availableField == nullptr
            || commandModeField == nullptr
            || suPathField == nullptr
            || cachedField == nullptr
            || cacheExpireAtField == nullptr
            || errorField == nullptr
            || attemptsField == nullptr) {
        clearExceptionIfNeeded(env);
        env->DeleteLocalRef(statusClass);
        env->DeleteLocalRef(statusObject);
        return rootStatusFailure("root status fields are not available");
    }

    RootStatusResult status;
    status.available = env->GetBooleanField(statusObject, availableField) == JNI_TRUE;
    status.cached = env->GetBooleanField(statusObject, cachedField) == JNI_TRUE;
    status.cacheExpireAt = static_cast<long long>(env->GetLongField(statusObject, cacheExpireAtField));

    jstring commandMode = static_cast<jstring>(env->GetObjectField(statusObject, commandModeField));
    jstring suPath = static_cast<jstring>(env->GetObjectField(statusObject, suPathField));
    jstring error = static_cast<jstring>(env->GetObjectField(statusObject, errorField));
    jobject attempts = env->GetObjectField(statusObject, attemptsField);
    status.commandMode = jStringToString(env, commandMode);
    status.suPath = jStringToString(env, suPath);
    status.error = jStringToString(env, error);
    readProbeAttempts(env, attempts, &status);

    if (commandMode != nullptr) {
        env->DeleteLocalRef(commandMode);
    }
    if (suPath != nullptr) {
        env->DeleteLocalRef(suPath);
    }
    if (error != nullptr) {
        env->DeleteLocalRef(error);
    }
    if (attempts != nullptr) {
        env->DeleteLocalRef(attempts);
    }
    env->DeleteLocalRef(statusClass);
    env->DeleteLocalRef(statusObject);
    return status;
}

ScreenCaptureResult AndroidBridge::captureRootScreen(unsigned char** pixels, size_t* capacity) {
    JNIEnv* env = getEnv();
    jmethodID methodId = staticMethod(
            env,
            "captureRootScreen",
            "(Ljava/nio/ByteBuffer;I)Lcom/autolua/engine/ScreenCaptureResult;"
    );
    if (env == nullptr || methodId == nullptr) {
        return captureFailure("root capture method is not available");
    }

    jobject targetBuffer = nullptr;
    jint targetCapacity = 0;
    if (pixels != nullptr
            && capacity != nullptr
            && *pixels != nullptr
            && *capacity > 0
            && *capacity <= static_cast<size_t>(std::numeric_limits<jint>::max())) {
        targetBuffer = env->NewDirectByteBuffer(*pixels, static_cast<jlong>(*capacity));
        if (clearExceptionIfNeeded(env) || targetBuffer == nullptr) {
            targetBuffer = nullptr;
            targetCapacity = 0;
        } else {
            targetCapacity = static_cast<jint>(*capacity);
        }
    }

    jobject resultObject = env->CallStaticObjectMethod(
            gBridgeClass,
            methodId,
            targetBuffer,
            targetCapacity
    );
    if (targetBuffer != nullptr) {
        env->DeleteLocalRef(targetBuffer);
    }
    if (clearExceptionIfNeeded(env)) {
        return captureFailure("root capture java call failed");
    }

    ScreenCaptureResult result = readScreenCaptureResult(env, resultObject, pixels, capacity);
    if (resultObject != nullptr) {
        env->DeleteLocalRef(resultObject);
    }
    return result;
}

bool AndroidBridge::touchDown(int id, int x, int y) {
    return callBoolean3Int("touchDown", id, x, y);
}

bool AndroidBridge::touchMove(int id, int x, int y) {
    return callBoolean3Int("touchMove", id, x, y);
}

bool AndroidBridge::touchUp(int id) {
    return callBoolean1Int("touchUp", id);
}

bool AndroidBridge::keyDown(int keyCode) {
    return callBoolean1Int("keyDown", keyCode);
}

bool AndroidBridge::keyUp(int keyCode) {
    return callBoolean1Int("keyUp", keyCode);
}

bool AndroidBridge::keyPress(int keyCode) {
    return callBoolean1Int("keyPress", keyCode);
}

bool AndroidBridge::inputText(const std::string& text) {
    return callBooleanString("inputText", text);
}
