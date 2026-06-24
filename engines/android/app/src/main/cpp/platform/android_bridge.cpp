#include "android_bridge.h"

#include <cstring>

namespace {

JavaVM* gJavaVm = nullptr;

JNIEnv* getEnv() {
    if (gJavaVm == nullptr) {
        return nullptr;
    }

    JNIEnv* env = nullptr;
    jint result = gJavaVm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (result == JNI_OK) {
        return env;
    }

    if (result == JNI_EDETACHED) {
        if (gJavaVm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
            return env;
        }
    }

    return nullptr;
}

bool callStaticBooleanMethod0(const char* methodName, const char* signature) {
    JNIEnv* env = getEnv();
    if (env == nullptr) {
        return false;
    }

    jclass bridgeClass = env->FindClass("com/autolua/engine/AndroidHostBridge");
    if (bridgeClass == nullptr) {
        env->ExceptionClear();
        return false;
    }

    jmethodID methodId = env->GetStaticMethodID(bridgeClass, methodName, signature);
    if (methodId == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        return false;
    }

    jboolean result = env->CallStaticBooleanMethod(bridgeClass, methodId);

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        return false;
    }

    env->DeleteLocalRef(bridgeClass);
    return result == JNI_TRUE;
}

bool callStaticBooleanMethod2(const char* methodName, const char* signature, jint x, jint y) {
    JNIEnv* env = getEnv();
    if (env == nullptr) {
        return false;
    }

    jclass bridgeClass = env->FindClass("com/autolua/engine/AndroidHostBridge");
    if (bridgeClass == nullptr) {
        env->ExceptionClear();
        return false;
    }

    jmethodID methodId = env->GetStaticMethodID(bridgeClass, methodName, signature);
    if (methodId == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        return false;
    }

    jboolean result = env->CallStaticBooleanMethod(bridgeClass, methodId, x, y);

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        return false;
    }

    env->DeleteLocalRef(bridgeClass);
    return result == JNI_TRUE;
}

bool callStaticBooleanMethod1(const char* methodName, const char* signature, jint value) {
    JNIEnv* env = getEnv();
    if (env == nullptr) {
        return false;
    }

    jclass bridgeClass = env->FindClass("com/autolua/engine/AndroidHostBridge");
    if (bridgeClass == nullptr) {
        env->ExceptionClear();
        return false;
    }

    jmethodID methodId = env->GetStaticMethodID(bridgeClass, methodName, signature);
    if (methodId == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        return false;
    }

    jboolean result = env->CallStaticBooleanMethod(bridgeClass, methodId, value);

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        return false;
    }

    env->DeleteLocalRef(bridgeClass);
    return result == JNI_TRUE;
}

bool callStaticBooleanMethod5(const char* methodName,
                              const char* signature,
                              jint x1,
                              jint y1,
                              jint x2,
                              jint y2,
                              jint durationMs) {
    JNIEnv* env = getEnv();
    if (env == nullptr) {
        return false;
    }

    jclass bridgeClass = env->FindClass("com/autolua/engine/AndroidHostBridge");
    if (bridgeClass == nullptr) {
        env->ExceptionClear();
        return false;
    }

    jmethodID methodId = env->GetStaticMethodID(bridgeClass, methodName, signature);
    if (methodId == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        return false;
    }

    jboolean result = env->CallStaticBooleanMethod(bridgeClass, methodId, x1, y1, x2, y2, durationMs);

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        return false;
    }

    env->DeleteLocalRef(bridgeClass);
    return result == JNI_TRUE;
}

bool callStaticBooleanStringMethod(const char* methodName,
                                   const char* signature,
                                   const std::string& value) {
    JNIEnv* env = getEnv();
    if (env == nullptr) {
        return false;
    }

    jclass bridgeClass = env->FindClass("com/autolua/engine/AndroidHostBridge");
    if (bridgeClass == nullptr) {
        env->ExceptionClear();
        return false;
    }

    jmethodID methodId = env->GetStaticMethodID(bridgeClass, methodName, signature);
    if (methodId == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        return false;
    }

    jstring javaValue = env->NewStringUTF(value.c_str());
    if (javaValue == nullptr) {
        env->DeleteLocalRef(bridgeClass);
        return false;
    }

    jboolean result = env->CallStaticBooleanMethod(bridgeClass, methodId, javaValue);

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(javaValue);
        env->DeleteLocalRef(bridgeClass);
        return false;
    }

    env->DeleteLocalRef(javaValue);
    env->DeleteLocalRef(bridgeClass);
    return result == JNI_TRUE;
}

bool callStaticBooleanStringStringMethod(const char* methodName,
                                         const char* signature,
                                         const std::string& first,
                                         const std::string& second) {
    JNIEnv* env = getEnv();
    if (env == nullptr) {
        return false;
    }

    jclass bridgeClass = env->FindClass("com/autolua/engine/AndroidHostBridge");
    if (bridgeClass == nullptr) {
        env->ExceptionClear();
        return false;
    }

    jmethodID methodId = env->GetStaticMethodID(bridgeClass, methodName, signature);
    if (methodId == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        return false;
    }

    jstring firstString = env->NewStringUTF(first.c_str());
    jstring secondString = env->NewStringUTF(second.c_str());
    if (firstString == nullptr || secondString == nullptr) {
        if (firstString != nullptr) {
            env->DeleteLocalRef(firstString);
        }
        if (secondString != nullptr) {
            env->DeleteLocalRef(secondString);
        }
        env->DeleteLocalRef(bridgeClass);
        return false;
    }

    jboolean result = env->CallStaticBooleanMethod(
            bridgeClass,
            methodId,
            firstString,
            secondString
    );

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(secondString);
        env->DeleteLocalRef(firstString);
        env->DeleteLocalRef(bridgeClass);
        return false;
    }

    env->DeleteLocalRef(secondString);
    env->DeleteLocalRef(firstString);
    env->DeleteLocalRef(bridgeClass);
    return result == JNI_TRUE;
}

bool callStaticBooleanStringBooleanMethod(const char* methodName,
                                          const char* signature,
                                          const std::string& value,
                                          bool flag) {
    JNIEnv* env = getEnv();
    if (env == nullptr) {
        return false;
    }

    jclass bridgeClass = env->FindClass("com/autolua/engine/AndroidHostBridge");
    if (bridgeClass == nullptr) {
        env->ExceptionClear();
        return false;
    }

    jmethodID methodId = env->GetStaticMethodID(bridgeClass, methodName, signature);
    if (methodId == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        return false;
    }

    jstring javaValue = env->NewStringUTF(value.c_str());
    if (javaValue == nullptr) {
        env->DeleteLocalRef(bridgeClass);
        return false;
    }

    jboolean result = env->CallStaticBooleanMethod(
            bridgeClass,
            methodId,
            javaValue,
            flag ? JNI_TRUE : JNI_FALSE
    );

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(javaValue);
        env->DeleteLocalRef(bridgeClass);
        return false;
    }

    env->DeleteLocalRef(javaValue);
    env->DeleteLocalRef(bridgeClass);
    return result == JNI_TRUE;
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

ScreenCaptureResult makeCaptureFailure(const std::string& error) {
    ScreenCaptureResult result;
    result.success = false;
    result.error = error;
    return result;
}

RootExecResult makeRootExecFailure(const std::string& error) {
    RootExecResult result;
    result.success = false;
    result.exitCode = -1;
    result.error = error;
    return result;
}

RootStatusResult makeRootStatusFailure(const std::string& error) {
    RootStatusResult result;
    result.available = false;
    result.error = error;
    return result;
}

RootExecResult readRootExecResult(JNIEnv* env,
                                  jobject resultObject,
                                  jclass bridgeClass,
                                  const std::string& failurePrefix) {
    if (resultObject == nullptr) {
        env->DeleteLocalRef(bridgeClass);
        return makeRootExecFailure(failurePrefix + " returned empty result");
    }

    jclass resultClass = env->GetObjectClass(resultObject);
    if (resultClass == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(resultObject);
        env->DeleteLocalRef(bridgeClass);
        return makeRootExecFailure(failurePrefix + " result class is not available");
    }

    jfieldID successField = env->GetFieldID(resultClass, "success", "Z");
    jfieldID exitCodeField = env->GetFieldID(resultClass, "exitCode", "I");
    jfieldID stdoutField = env->GetFieldID(resultClass, "stdout", "Ljava/lang/String;");
    jfieldID stderrField = env->GetFieldID(resultClass, "stderr", "Ljava/lang/String;");
    jfieldID timedOutField = env->GetFieldID(resultClass, "timedOut", "Z");
    jfieldID errorField = env->GetFieldID(resultClass, "error", "Ljava/lang/String;");
    if (successField == nullptr
            || exitCodeField == nullptr
            || stdoutField == nullptr
            || stderrField == nullptr
            || timedOutField == nullptr
            || errorField == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(resultClass);
        env->DeleteLocalRef(resultObject);
        env->DeleteLocalRef(bridgeClass);
        return makeRootExecFailure(failurePrefix + " result fields are not available");
    }

    RootExecResult result;
    result.success = env->GetBooleanField(resultObject, successField) == JNI_TRUE;
    result.exitCode = env->GetIntField(resultObject, exitCodeField);
    result.timedOut = env->GetBooleanField(resultObject, timedOutField) == JNI_TRUE;

    jstring stdoutValue = static_cast<jstring>(env->GetObjectField(resultObject, stdoutField));
    jstring stderrValue = static_cast<jstring>(env->GetObjectField(resultObject, stderrField));
    jstring errorValue = static_cast<jstring>(env->GetObjectField(resultObject, errorField));
    result.stdoutText = jStringToString(env, stdoutValue);
    result.stderrText = jStringToString(env, stderrValue);
    result.error = jStringToString(env, errorValue);

    if (stdoutValue != nullptr) {
        env->DeleteLocalRef(stdoutValue);
    }
    if (stderrValue != nullptr) {
        env->DeleteLocalRef(stderrValue);
    }
    if (errorValue != nullptr) {
        env->DeleteLocalRef(errorValue);
    }
    env->DeleteLocalRef(resultClass);
    env->DeleteLocalRef(resultObject);
    env->DeleteLocalRef(bridgeClass);

    return result;
}

RootExecResult callStaticRootResultNoArgMethod(const char* methodName,
                                               const char* signature,
                                               const std::string& failurePrefix) {
    JNIEnv* env = getEnv();
    if (env == nullptr) {
        return makeRootExecFailure("jni environment is not available");
    }

    jclass bridgeClass = env->FindClass("com/autolua/engine/AndroidHostBridge");
    if (bridgeClass == nullptr) {
        env->ExceptionClear();
        return makeRootExecFailure("android host bridge is not available");
    }

    jmethodID methodId = env->GetStaticMethodID(bridgeClass, methodName, signature);
    if (methodId == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        return makeRootExecFailure(failurePrefix + " method is not available");
    }

    jobject resultObject = env->CallStaticObjectMethod(bridgeClass, methodId);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        return makeRootExecFailure(failurePrefix + " java call failed");
    }

    return readRootExecResult(env, resultObject, bridgeClass, failurePrefix);
}

RootExecResult callStaticRootResultStringMethod(const char* methodName,
                                                const char* signature,
                                                const std::string& path,
                                                const std::string& failurePrefix) {
    JNIEnv* env = getEnv();
    if (env == nullptr) {
        return makeRootExecFailure("jni environment is not available");
    }

    jclass bridgeClass = env->FindClass("com/autolua/engine/AndroidHostBridge");
    if (bridgeClass == nullptr) {
        env->ExceptionClear();
        return makeRootExecFailure("android host bridge is not available");
    }

    jmethodID methodId = env->GetStaticMethodID(bridgeClass, methodName, signature);
    if (methodId == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        return makeRootExecFailure(failurePrefix + " method is not available");
    }

    jstring pathString = env->NewStringUTF(path.c_str());
    if (pathString == nullptr) {
        env->DeleteLocalRef(bridgeClass);
        return makeRootExecFailure("root file path string is invalid");
    }

    jobject resultObject = env->CallStaticObjectMethod(bridgeClass, methodId, pathString);
    env->DeleteLocalRef(pathString);

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        return makeRootExecFailure(failurePrefix + " java call failed");
    }

    return readRootExecResult(env, resultObject, bridgeClass, failurePrefix);
}

RootExecResult callStaticRootResultStringIntMethod(const char* methodName,
                                                   const char* signature,
                                                   const std::string& value,
                                                   int number,
                                                   const std::string& failurePrefix) {
    JNIEnv* env = getEnv();
    if (env == nullptr) {
        return makeRootExecFailure("jni environment is not available");
    }

    jclass bridgeClass = env->FindClass("com/autolua/engine/AndroidHostBridge");
    if (bridgeClass == nullptr) {
        env->ExceptionClear();
        return makeRootExecFailure("android host bridge is not available");
    }

    jmethodID methodId = env->GetStaticMethodID(bridgeClass, methodName, signature);
    if (methodId == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        return makeRootExecFailure(failurePrefix + " method is not available");
    }

    jstring javaValue = env->NewStringUTF(value.c_str());
    if (javaValue == nullptr) {
        env->DeleteLocalRef(bridgeClass);
        return makeRootExecFailure(failurePrefix + " string is invalid");
    }

    jobject resultObject = env->CallStaticObjectMethod(
            bridgeClass,
            methodId,
            javaValue,
            static_cast<jint>(number)
    );
    env->DeleteLocalRef(javaValue);

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        return makeRootExecFailure(failurePrefix + " java call failed");
    }

    return readRootExecResult(env, resultObject, bridgeClass, failurePrefix);
}

RootExecResult callStaticRootResultStringBooleanMethod(const char* methodName,
                                                       const char* signature,
                                                       const std::string& value,
                                                       bool flag,
                                                       const std::string& failurePrefix) {
    JNIEnv* env = getEnv();
    if (env == nullptr) {
        return makeRootExecFailure("jni environment is not available");
    }

    jclass bridgeClass = env->FindClass("com/autolua/engine/AndroidHostBridge");
    if (bridgeClass == nullptr) {
        env->ExceptionClear();
        return makeRootExecFailure("android host bridge is not available");
    }

    jmethodID methodId = env->GetStaticMethodID(bridgeClass, methodName, signature);
    if (methodId == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        return makeRootExecFailure(failurePrefix + " method is not available");
    }

    jstring javaValue = env->NewStringUTF(value.c_str());
    if (javaValue == nullptr) {
        env->DeleteLocalRef(bridgeClass);
        return makeRootExecFailure(failurePrefix + " string is invalid");
    }

    jobject resultObject = env->CallStaticObjectMethod(
            bridgeClass,
            methodId,
            javaValue,
            flag ? JNI_TRUE : JNI_FALSE
    );
    env->DeleteLocalRef(javaValue);

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        return makeRootExecFailure(failurePrefix + " java call failed");
    }

    return readRootExecResult(env, resultObject, bridgeClass, failurePrefix);
}

RootExecResult callStaticRootResultStringStringMethod(const char* methodName,
                                                      const char* signature,
                                                      const std::string& first,
                                                      const std::string& second,
                                                      const std::string& failurePrefix) {
    JNIEnv* env = getEnv();
    if (env == nullptr) {
        return makeRootExecFailure("jni environment is not available");
    }

    jclass bridgeClass = env->FindClass("com/autolua/engine/AndroidHostBridge");
    if (bridgeClass == nullptr) {
        env->ExceptionClear();
        return makeRootExecFailure("android host bridge is not available");
    }

    jmethodID methodId = env->GetStaticMethodID(bridgeClass, methodName, signature);
    if (methodId == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        return makeRootExecFailure(failurePrefix + " method is not available");
    }

    jstring firstString = env->NewStringUTF(first.c_str());
    jstring secondString = env->NewStringUTF(second.c_str());
    if (firstString == nullptr || secondString == nullptr) {
        if (firstString != nullptr) {
            env->DeleteLocalRef(firstString);
        }
        if (secondString != nullptr) {
            env->DeleteLocalRef(secondString);
        }
        env->DeleteLocalRef(bridgeClass);
        return makeRootExecFailure(failurePrefix + " string is invalid");
    }

    jobject resultObject = env->CallStaticObjectMethod(
            bridgeClass,
            methodId,
            firstString,
            secondString
    );
    env->DeleteLocalRef(firstString);
    env->DeleteLocalRef(secondString);

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        return makeRootExecFailure(failurePrefix + " java call failed");
    }

    return readRootExecResult(env, resultObject, bridgeClass, failurePrefix);
}

RootProbeAttempt readRootProbeAttempt(JNIEnv* env, jobject attemptObject) {
    RootProbeAttempt attempt;
    if (attemptObject == nullptr) {
        attempt.error = "root probe attempt is empty";
        return attempt;
    }

    jclass attemptClass = env->GetObjectClass(attemptObject);
    if (attemptClass == nullptr) {
        env->ExceptionClear();
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
        env->ExceptionClear();
        env->DeleteLocalRef(attemptClass);
        attempt.error = "root probe attempt fields are not available";
        return attempt;
    }

    jstring commandMode = static_cast<jstring>(env->GetObjectField(attemptObject, commandModeField));
    jstring suPath = static_cast<jstring>(env->GetObjectField(attemptObject, suPathField));
    jstring stdoutValue = static_cast<jstring>(env->GetObjectField(attemptObject, stdoutField));
    jstring stderrValue = static_cast<jstring>(env->GetObjectField(attemptObject, stderrField));
    jstring errorValue = static_cast<jstring>(env->GetObjectField(attemptObject, errorField));

    attempt.commandMode = jStringToString(env, commandMode);
    attempt.suPath = jStringToString(env, suPath);
    attempt.exitCode = env->GetIntField(attemptObject, exitCodeField);
    attempt.stdoutText = jStringToString(env, stdoutValue);
    attempt.stderrText = jStringToString(env, stderrValue);
    attempt.timedOut = env->GetBooleanField(attemptObject, timedOutField) == JNI_TRUE;
    attempt.error = jStringToString(env, errorValue);

    if (commandMode != nullptr) {
        env->DeleteLocalRef(commandMode);
    }
    if (suPath != nullptr) {
        env->DeleteLocalRef(suPath);
    }
    if (stdoutValue != nullptr) {
        env->DeleteLocalRef(stdoutValue);
    }
    if (stderrValue != nullptr) {
        env->DeleteLocalRef(stderrValue);
    }
    if (errorValue != nullptr) {
        env->DeleteLocalRef(errorValue);
    }
    env->DeleteLocalRef(attemptClass);
    return attempt;
}

} // namespace

void AndroidBridge::init(JavaVM* javaVm) {
    gJavaVm = javaVm;
}

bool AndroidBridge::isAccessibilityEnabled() {
    return callStaticBooleanMethod0("isAccessibilityEnabled", "()Z");
}

bool AndroidBridge::isRootModeEnabled() {
    return callStaticBooleanMethod0("isRootModeEnabled", "()Z");
}

bool AndroidBridge::setRootModeEnabled(bool enabled) {
    return callStaticBooleanMethod1(
            "setRootModeEnabled",
            "(Z)Z",
            enabled ? JNI_TRUE : JNI_FALSE
    );
}

bool AndroidBridge::isRootAvailable() {
    return callStaticBooleanMethod0("isRootAvailable", "()Z");
}

RootStatusResult AndroidBridge::rootStatus() {
    JNIEnv* env = getEnv();
    if (env == nullptr) {
        return makeRootStatusFailure("jni environment is not available");
    }

    jclass bridgeClass = env->FindClass("com/autolua/engine/AndroidHostBridge");
    if (bridgeClass == nullptr) {
        env->ExceptionClear();
        return makeRootStatusFailure("android host bridge is not available");
    }

    jmethodID methodId = env->GetStaticMethodID(
            bridgeClass,
            "rootStatus",
            "()Lcom/autolua/engine/RootStatus;"
    );
    if (methodId == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        return makeRootStatusFailure("root status method is not available");
    }

    jobject statusObject = env->CallStaticObjectMethod(bridgeClass, methodId);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        return makeRootStatusFailure("root status java call failed");
    }

    if (statusObject == nullptr) {
        env->DeleteLocalRef(bridgeClass);
        return makeRootStatusFailure("root status returned empty result");
    }

    jclass statusClass = env->GetObjectClass(statusObject);
    if (statusClass == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(statusObject);
        env->DeleteLocalRef(bridgeClass);
        return makeRootStatusFailure("root status class is not available");
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
        env->ExceptionClear();
        env->DeleteLocalRef(statusClass);
        env->DeleteLocalRef(statusObject);
        env->DeleteLocalRef(bridgeClass);
        return makeRootStatusFailure("root status fields are not available");
    }

    RootStatusResult result;
    result.available = env->GetBooleanField(statusObject, availableField) == JNI_TRUE;
    result.cached = env->GetBooleanField(statusObject, cachedField) == JNI_TRUE;
    result.cacheExpireAt = static_cast<long long>(env->GetLongField(statusObject, cacheExpireAtField));

    jstring commandMode = static_cast<jstring>(env->GetObjectField(statusObject, commandModeField));
    jstring suPath = static_cast<jstring>(env->GetObjectField(statusObject, suPathField));
    jstring error = static_cast<jstring>(env->GetObjectField(statusObject, errorField));
    result.commandMode = jStringToString(env, commandMode);
    result.suPath = jStringToString(env, suPath);
    result.error = jStringToString(env, error);

    jobject attemptsList = env->GetObjectField(statusObject, attemptsField);
    if (attemptsList != nullptr) {
        jclass listClass = env->FindClass("java/util/List");
        jmethodID sizeMethod = listClass == nullptr ? nullptr : env->GetMethodID(listClass, "size", "()I");
        jmethodID getMethod = listClass == nullptr ? nullptr : env->GetMethodID(listClass, "get", "(I)Ljava/lang/Object;");
        if (listClass != nullptr && sizeMethod != nullptr && getMethod != nullptr) {
            jint size = env->CallIntMethod(attemptsList, sizeMethod);
            if (!env->ExceptionCheck()) {
                for (jint i = 0; i < size; ++i) {
                    jobject attemptObject = env->CallObjectMethod(attemptsList, getMethod, i);
                    if (env->ExceptionCheck()) {
                        env->ExceptionClear();
                        break;
                    }
                    result.attempts.push_back(readRootProbeAttempt(env, attemptObject));
                    if (attemptObject != nullptr) {
                        env->DeleteLocalRef(attemptObject);
                    }
                }
            } else {
                env->ExceptionClear();
            }
        } else {
            env->ExceptionClear();
        }
        if (listClass != nullptr) {
            env->DeleteLocalRef(listClass);
        }
        env->DeleteLocalRef(attemptsList);
    }

    if (commandMode != nullptr) {
        env->DeleteLocalRef(commandMode);
    }
    if (suPath != nullptr) {
        env->DeleteLocalRef(suPath);
    }
    if (error != nullptr) {
        env->DeleteLocalRef(error);
    }
    env->DeleteLocalRef(statusClass);
    env->DeleteLocalRef(statusObject);
    env->DeleteLocalRef(bridgeClass);
    return result;
}

RootExecResult AndroidBridge::rootExec(const std::string& command, int timeoutMs) {
    JNIEnv* env = getEnv();
    if (env == nullptr) {
        return makeRootExecFailure("jni environment is not available");
    }

    jclass bridgeClass = env->FindClass("com/autolua/engine/AndroidHostBridge");
    if (bridgeClass == nullptr) {
        env->ExceptionClear();
        return makeRootExecFailure("android host bridge is not available");
    }

    jmethodID methodId = env->GetStaticMethodID(
            bridgeClass,
            "rootExec",
            "(Ljava/lang/String;I)Lcom/autolua/engine/RootCommandResult;"
    );
    if (methodId == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        return makeRootExecFailure("root exec method is not available");
    }

    jstring commandString = env->NewStringUTF(command.c_str());
    if (commandString == nullptr) {
        env->DeleteLocalRef(bridgeClass);
        return makeRootExecFailure("root command string is invalid");
    }

    jobject resultObject = env->CallStaticObjectMethod(
            bridgeClass,
            methodId,
            commandString,
            static_cast<jint>(timeoutMs)
    );
    env->DeleteLocalRef(commandString);

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        return makeRootExecFailure("root exec java call failed");
    }

    return readRootExecResult(env, resultObject, bridgeClass, "root exec");
}

RootExecResult AndroidBridge::rootFileExists(const std::string& path) {
    return callStaticRootResultStringMethod(
            "rootFileExists",
            "(Ljava/lang/String;)Lcom/autolua/engine/RootCommandResult;",
            path,
            "root file exists"
    );
}

RootExecResult AndroidBridge::rootFileReadText(const std::string& path, int timeoutMs) {
    JNIEnv* env = getEnv();
    if (env == nullptr) {
        return makeRootExecFailure("jni environment is not available");
    }

    jclass bridgeClass = env->FindClass("com/autolua/engine/AndroidHostBridge");
    if (bridgeClass == nullptr) {
        env->ExceptionClear();
        return makeRootExecFailure("android host bridge is not available");
    }

    jmethodID methodId = env->GetStaticMethodID(
            bridgeClass,
            "rootFileReadText",
            "(Ljava/lang/String;I)Lcom/autolua/engine/RootCommandResult;"
    );
    if (methodId == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        return makeRootExecFailure("root file read method is not available");
    }

    jstring pathString = env->NewStringUTF(path.c_str());
    if (pathString == nullptr) {
        env->DeleteLocalRef(bridgeClass);
        return makeRootExecFailure("root file path string is invalid");
    }

    jobject resultObject = env->CallStaticObjectMethod(
            bridgeClass,
            methodId,
            pathString,
            static_cast<jint>(timeoutMs)
    );
    env->DeleteLocalRef(pathString);

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        return makeRootExecFailure("root file read java call failed");
    }

    return readRootExecResult(env, resultObject, bridgeClass, "root file read");
}

RootExecResult AndroidBridge::rootFileWriteText(const std::string& path,
                                                const std::string& content,
                                                int timeoutMs) {
    JNIEnv* env = getEnv();
    if (env == nullptr) {
        return makeRootExecFailure("jni environment is not available");
    }

    jclass bridgeClass = env->FindClass("com/autolua/engine/AndroidHostBridge");
    if (bridgeClass == nullptr) {
        env->ExceptionClear();
        return makeRootExecFailure("android host bridge is not available");
    }

    jmethodID methodId = env->GetStaticMethodID(
            bridgeClass,
            "rootFileWriteText",
            "(Ljava/lang/String;Ljava/lang/String;I)Lcom/autolua/engine/RootCommandResult;"
    );
    if (methodId == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        return makeRootExecFailure("root file write method is not available");
    }

    jstring pathString = env->NewStringUTF(path.c_str());
    jstring contentString = env->NewStringUTF(content.c_str());
    if (pathString == nullptr || contentString == nullptr) {
        if (pathString != nullptr) {
            env->DeleteLocalRef(pathString);
        }
        if (contentString != nullptr) {
            env->DeleteLocalRef(contentString);
        }
        env->DeleteLocalRef(bridgeClass);
        return makeRootExecFailure("root file write string is invalid");
    }

    jobject resultObject = env->CallStaticObjectMethod(
            bridgeClass,
            methodId,
            pathString,
            contentString,
            static_cast<jint>(timeoutMs)
    );
    env->DeleteLocalRef(pathString);
    env->DeleteLocalRef(contentString);

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        return makeRootExecFailure("root file write java call failed");
    }

    return readRootExecResult(env, resultObject, bridgeClass, "root file write");
}

RootExecResult AndroidBridge::rootFileStat(const std::string& path) {
    return callStaticRootResultStringMethod(
            "rootFileStat",
            "(Ljava/lang/String;)Lcom/autolua/engine/RootCommandResult;",
            path,
            "root file stat"
    );
}

RootExecResult AndroidBridge::rootFileList(const std::string& path) {
    return callStaticRootResultStringMethod(
            "rootFileList",
            "(Ljava/lang/String;)Lcom/autolua/engine/RootCommandResult;",
            path,
            "root file list"
    );
}

RootExecResult AndroidBridge::rootFileRemove(const std::string& path, bool recursive) {
    return callStaticRootResultStringBooleanMethod(
            "rootFileRemove",
            "(Ljava/lang/String;Z)Lcom/autolua/engine/RootCommandResult;",
            path,
            recursive,
            "root file remove"
    );
}

RootExecResult AndroidBridge::rootFileMkdir(const std::string& path, bool recursive) {
    return callStaticRootResultStringBooleanMethod(
            "rootFileMkdir",
            "(Ljava/lang/String;Z)Lcom/autolua/engine/RootCommandResult;",
            path,
            recursive,
            "root file mkdir"
    );
}

RootExecResult AndroidBridge::rootFileChmod(const std::string& path, const std::string& mode) {
    return callStaticRootResultStringStringMethod(
            "rootFileChmod",
            "(Ljava/lang/String;Ljava/lang/String;)Lcom/autolua/engine/RootCommandResult;",
            path,
            mode,
            "root file chmod"
    );
}

RootExecResult AndroidBridge::rootFileChown(const std::string& path, const std::string& owner) {
    return callStaticRootResultStringStringMethod(
            "rootFileChown",
            "(Ljava/lang/String;Ljava/lang/String;)Lcom/autolua/engine/RootCommandResult;",
            path,
            owner,
            "root file chown"
    );
}

RootExecResult AndroidBridge::rootProcessPidOf(const std::string& processName) {
    return callStaticRootResultStringMethod(
            "rootProcessPidOf",
            "(Ljava/lang/String;)Lcom/autolua/engine/RootCommandResult;",
            processName,
            "root process pidOf"
    );
}

RootExecResult AndroidBridge::rootProcessList() {
    return callStaticRootResultNoArgMethod(
            "rootProcessList",
            "()Lcom/autolua/engine/RootCommandResult;",
            "root process list"
    );
}

RootExecResult AndroidBridge::rootProcessInfo(const std::string& pidOrName) {
    return callStaticRootResultStringMethod(
            "rootProcessInfo",
            "(Ljava/lang/String;)Lcom/autolua/engine/RootCommandResult;",
            pidOrName,
            "root process info"
    );
}

RootExecResult AndroidBridge::rootProcessKill(const std::string& pidOrName, int signal) {
    return callStaticRootResultStringIntMethod(
            "rootProcessKill",
            "(Ljava/lang/String;I)Lcom/autolua/engine/RootCommandResult;",
            pidOrName,
            signal,
            "root process kill"
    );
}

RootExecResult AndroidBridge::deviceScreenState() {
    return callStaticRootResultNoArgMethod(
            "deviceScreenState",
            "()Lcom/autolua/engine/RootCommandResult;",
            "device screen state"
    );
}

RootExecResult AndroidBridge::deviceWake() {
    return callStaticRootResultNoArgMethod(
            "deviceWake",
            "()Lcom/autolua/engine/RootCommandResult;",
            "device wake"
    );
}

RootExecResult AndroidBridge::deviceSleep() {
    return callStaticRootResultNoArgMethod(
            "deviceSleep",
            "()Lcom/autolua/engine/RootCommandResult;",
            "device sleep"
    );
}

RootExecResult AndroidBridge::deviceBattery() {
    return callStaticRootResultNoArgMethod(
            "deviceBattery",
            "()Lcom/autolua/engine/RootCommandResult;",
            "device battery"
    );
}

RootExecResult AndroidBridge::deviceRotation() {
    return callStaticRootResultNoArgMethod(
            "deviceRotation",
            "()Lcom/autolua/engine/RootCommandResult;",
            "device rotation"
    );
}

RootExecResult AndroidBridge::deviceSetRotation(int rotation, bool locked) {
    JNIEnv* env = getEnv();
    if (env == nullptr) {
        return makeRootExecFailure("jni environment is not available");
    }

    jclass bridgeClass = env->FindClass("com/autolua/engine/AndroidHostBridge");
    if (bridgeClass == nullptr) {
        env->ExceptionClear();
        return makeRootExecFailure("android host bridge is not available");
    }

    jmethodID methodId = env->GetStaticMethodID(
            bridgeClass,
            "deviceSetRotation",
            "(IZ)Lcom/autolua/engine/RootCommandResult;"
    );
    if (methodId == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        return makeRootExecFailure("device set rotation method is not available");
    }

    jobject resultObject = env->CallStaticObjectMethod(
            bridgeClass,
            methodId,
            static_cast<jint>(rotation),
            locked ? JNI_TRUE : JNI_FALSE
    );
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        return makeRootExecFailure("device set rotation java call failed");
    }

    return readRootExecResult(env, resultObject, bridgeClass, "device set rotation");
}

bool AndroidBridge::appIsInstalled(const std::string& packageName) {
    return callStaticBooleanStringMethod("appIsInstalled", "(Ljava/lang/String;)Z", packageName);
}

bool AndroidBridge::appOpen(const std::string& packageName) {
    return callStaticBooleanStringMethod("appOpen", "(Ljava/lang/String;)Z", packageName);
}

bool AndroidBridge::appStop(const std::string& packageName) {
    return callStaticBooleanStringMethod("appStop", "(Ljava/lang/String;)Z", packageName);
}

bool AndroidBridge::appClearData(const std::string& packageName) {
    return callStaticBooleanStringMethod("appClearData", "(Ljava/lang/String;)Z", packageName);
}

bool AndroidBridge::appGrantPermission(const std::string& packageName,
                                       const std::string& permissionName) {
    return callStaticBooleanStringStringMethod(
            "appGrantPermission",
            "(Ljava/lang/String;Ljava/lang/String;)Z",
            packageName,
            permissionName
    );
}

bool AndroidBridge::appRevokePermission(const std::string& packageName,
                                        const std::string& permissionName) {
    return callStaticBooleanStringStringMethod(
            "appRevokePermission",
            "(Ljava/lang/String;Ljava/lang/String;)Z",
            packageName,
            permissionName
    );
}

RootExecResult AndroidBridge::appCurrent() {
    return callStaticRootResultNoArgMethod(
            "appCurrent",
            "()Lcom/autolua/engine/RootCommandResult;",
            "app current"
    );
}

bool AndroidBridge::appInstall(const std::string& apkPath, bool replace) {
    return callStaticBooleanStringBooleanMethod(
            "appInstall",
            "(Ljava/lang/String;Z)Z",
            apkPath,
            replace
    );
}

bool AndroidBridge::appUninstall(const std::string& packageName, bool keepData) {
    return callStaticBooleanStringBooleanMethod(
            "appUninstall",
            "(Ljava/lang/String;Z)Z",
            packageName,
            keepData
    );
}

bool AndroidBridge::appDisable(const std::string& packageName) {
    return callStaticBooleanStringMethod("appDisable", "(Ljava/lang/String;)Z", packageName);
}

bool AndroidBridge::appEnable(const std::string& packageName) {
    return callStaticBooleanStringMethod("appEnable", "(Ljava/lang/String;)Z", packageName);
}

bool AndroidBridge::hasScreenCapturePermission() {
    return callStaticBooleanMethod0("hasScreenCapturePermission", "()Z");
}

ScreenCaptureResult AndroidBridge::captureScreen() {
    JNIEnv* env = getEnv();
    if (env == nullptr) {
        return makeCaptureFailure("jni environment is not available");
    }

    jclass bridgeClass = env->FindClass("com/autolua/engine/AndroidHostBridge");
    if (bridgeClass == nullptr) {
        env->ExceptionClear();
        return makeCaptureFailure("android host bridge is not available");
    }

    jmethodID methodId = env->GetStaticMethodID(
            bridgeClass,
            "captureScreen",
            "()Lcom/autolua/engine/ScreenCaptureResult;"
    );
    if (methodId == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        return makeCaptureFailure("screen capture method is not available");
    }

    jobject resultObject = env->CallStaticObjectMethod(bridgeClass, methodId);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(bridgeClass);
        return makeCaptureFailure("screen capture java call failed");
    }

    if (resultObject == nullptr) {
        env->DeleteLocalRef(bridgeClass);
        return makeCaptureFailure("screen capture returned empty result");
    }

    jclass resultClass = env->GetObjectClass(resultObject);
    if (resultClass == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(resultObject);
        env->DeleteLocalRef(bridgeClass);
        return makeCaptureFailure("screen capture result class is not available");
    }

    jfieldID successField = env->GetFieldID(resultClass, "success", "Z");
    jfieldID pixelBufferField = env->GetFieldID(resultClass, "pixelBuffer", "Ljava/nio/ByteBuffer;");
    jfieldID widthField = env->GetFieldID(resultClass, "width", "I");
    jfieldID heightField = env->GetFieldID(resultClass, "height", "I");
    jfieldID rowStrideField = env->GetFieldID(resultClass, "rowStride", "I");
    jfieldID pixelStrideField = env->GetFieldID(resultClass, "pixelStride", "I");
    jfieldID formatField = env->GetFieldID(resultClass, "format", "Ljava/lang/String;");
    jfieldID errorField = env->GetFieldID(resultClass, "error", "Ljava/lang/String;");
    jmethodID closeMethod = env->GetMethodID(resultClass, "close", "()V");

    if (successField == nullptr
            || pixelBufferField == nullptr
            || widthField == nullptr
            || heightField == nullptr
            || rowStrideField == nullptr
            || pixelStrideField == nullptr
            || formatField == nullptr
            || errorField == nullptr
            || closeMethod == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(resultClass);
        env->DeleteLocalRef(resultObject);
        env->DeleteLocalRef(bridgeClass);
        return makeCaptureFailure("screen capture result fields are not available");
    }

    ScreenCaptureResult result;
    result.success = env->GetBooleanField(resultObject, successField) == JNI_TRUE;
    result.width = env->GetIntField(resultObject, widthField);
    result.height = env->GetIntField(resultObject, heightField);
    result.rowStride = env->GetIntField(resultObject, rowStrideField);
    result.pixelStride = env->GetIntField(resultObject, pixelStrideField);

    jobject pixelBuffer = env->GetObjectField(resultObject, pixelBufferField);
    jstring format = static_cast<jstring>(env->GetObjectField(resultObject, formatField));
    jstring error = static_cast<jstring>(env->GetObjectField(resultObject, errorField));
    result.format = jStringToString(env, format);
    result.error = jStringToString(env, error);

    if (result.success && pixelBuffer != nullptr) {
        auto* source = static_cast<unsigned char*>(env->GetDirectBufferAddress(pixelBuffer));
        jlong sourceCapacity = env->GetDirectBufferCapacity(pixelBuffer);

        int compactRowStride = result.width * 4;
        size_t targetSize = static_cast<size_t>(compactRowStride) * static_cast<size_t>(result.height);
        size_t requiredSourceSize = static_cast<size_t>(result.rowStride)
                * static_cast<size_t>(result.height - 1)
                + static_cast<size_t>(result.width) * static_cast<size_t>(result.pixelStride);

        if (source == nullptr || sourceCapacity <= 0) {
            result.success = false;
            result.error = "screen capture pixel buffer is not direct";
        } else if (static_cast<size_t>(sourceCapacity) < requiredSourceSize) {
            result.success = false;
            result.error = "screen capture pixel buffer is incomplete";
        } else if (result.pixelStride < 4) {
            result.success = false;
            result.error = "screen capture pixel stride is unsupported";
        } else if (result.pixelStride == 4) {
            result.pixels.resize(targetSize);
            for (int y = 0; y < result.height; ++y) {
                const unsigned char* sourceRow = source + static_cast<size_t>(y) * result.rowStride;
                unsigned char* targetRow = result.pixels.data()
                        + static_cast<size_t>(y) * compactRowStride;
                std::memcpy(targetRow, sourceRow, static_cast<size_t>(compactRowStride));
            }
            result.rowStride = compactRowStride;
            result.pixelStride = 4;
        } else {
            result.pixels.resize(targetSize);
            for (int y = 0; y < result.height; ++y) {
                const unsigned char* sourceRow = source + static_cast<size_t>(y) * result.rowStride;
                unsigned char* targetRow = result.pixels.data()
                        + static_cast<size_t>(y) * compactRowStride;

                for (int x = 0; x < result.width; ++x) {
                    const unsigned char* sourcePixel = sourceRow
                            + static_cast<size_t>(x) * result.pixelStride;
                    unsigned char* targetPixel = targetRow + static_cast<size_t>(x) * 4;
                    targetPixel[0] = sourcePixel[0];
                    targetPixel[1] = sourcePixel[1];
                    targetPixel[2] = sourcePixel[2];
                    targetPixel[3] = sourcePixel[3];
                }
            }
            result.rowStride = compactRowStride;
            result.pixelStride = 4;
        }
    } else if (result.success) {
        result.success = false;
        result.error = "screen capture pixel buffer is empty";
    }

    env->CallVoidMethod(resultObject, closeMethod);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    if (pixelBuffer != nullptr) {
        env->DeleteLocalRef(pixelBuffer);
    }
    if (format != nullptr) {
        env->DeleteLocalRef(format);
    }
    if (error != nullptr) {
        env->DeleteLocalRef(error);
    }
    env->DeleteLocalRef(resultClass);
    env->DeleteLocalRef(resultObject);
    env->DeleteLocalRef(bridgeClass);

    if (!result.success && result.error.empty()) {
        result.error = "screen capture failed";
    }

    return result;
}

bool AndroidBridge::touchTap(int x, int y) {
    return callStaticBooleanMethod2("touchTap", "(II)Z", x, y);
}

bool AndroidBridge::touchSwipe(int x1, int y1, int x2, int y2, int durationMs) {
    return callStaticBooleanMethod5("touchSwipe", "(IIIII)Z", x1, y1, x2, y2, durationMs);
}

bool AndroidBridge::inputText(const std::string& text) {
    return callStaticBooleanStringMethod("inputText", "(Ljava/lang/String;)Z", text);
}

bool AndroidBridge::pasteText(const std::string& text) {
    return callStaticBooleanStringMethod("pasteText", "(Ljava/lang/String;)Z", text);
}

bool AndroidBridge::keyPress(int keyCode) {
    return callStaticBooleanMethod1("keyPress", "(I)Z", static_cast<jint>(keyCode));
}

bool AndroidBridge::keyBack() {
    return callStaticBooleanMethod0("keyBack", "()Z");
}

bool AndroidBridge::keyHome() {
    return callStaticBooleanMethod0("keyHome", "()Z");
}
