/**
 * 文件用途：实现懒人精灵兼容的 import 和 Lua-Java 动态对象调用桥。
 */
#include "java_bridge.h"

#include "lua_runtime.h"
#include "../../core/system_c_api.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

namespace {

constexpr const char* kJavaObjectMetatable = "AutoLuaEngine.JavaObject";
constexpr const char* kJavaContextTokenKey = "AutoLuaEngine.JavaContextToken";

/**
 * Java 对象在 Lua userdata 中保存的最小信息。
 *
 * object 是 JNI GlobalRef，保证 Java 对象在 Lua userdata 存活期间不会被 GC。
 * contextToken 用于确认该对象仍属于一个有效的 LuaRuntime。
 */
struct JavaObjectUserdata {
    jobject object = nullptr;
    std::uint64_t contextToken = 0;
    bool isClass = false;
};

/**
 * 其他 Java 线程提交给脚本线程执行的一次 Lua 接口回调。
 */
struct LuaCallbackRequest {
    int reference = LUA_NOREF;
    bool waitForResult = true;
    std::vector<jobject> arguments;
    jobject result = nullptr;
    std::string error;
    bool done = false;
    std::mutex mutex;
    std::condition_variable condition;
};

/**
 * 单个 LuaRuntime 的 Java 互操作状态。
 */
struct LuaJavaContext {
    lua_State* state = nullptr;
    LuaRuntime* runtime = nullptr;
    std::uint64_t token = 0;
    std::thread::id ownerThread;
    std::atomic_bool alive{true};
    std::atomic_int callbackDepth{0};
    bool processingCallbacks = false;

    std::vector<std::string> importedPackages;
    std::vector<int> callbackReferences;
    int previousGlobalIndexReference = LUA_NOREF;

    std::mutex stateMutex;
    std::condition_variable stateCondition;
    int javaCallDepth = 0;
    int activeDirectCallbacks = 0;

    std::recursive_mutex luaCallbackMutex;
    std::mutex callbackQueueMutex;
    std::deque<std::shared_ptr<LuaCallbackRequest>> callbackQueue;
};

/**
 * 互操作层反复使用的 Java 类、方法和字段缓存。
 */
struct JavaInteropCache {
    bool ready = false;
    std::string error;

    jclass bridgeClass = nullptr;
    jclass memberResultClass = nullptr;
    jclass invocationResultClass = nullptr;
    jclass luaTableClass = nullptr;
    jclass luaCallbackClass = nullptr;
    jclass objectClass = nullptr;
    jclass classClass = nullptr;
    jclass stringClass = nullptr;
    jclass numberClass = nullptr;
    jclass booleanClass = nullptr;
    jclass characterClass = nullptr;
    jclass byteClass = nullptr;
    jclass shortClass = nullptr;
    jclass integerClass = nullptr;
    jclass longClass = nullptr;
    jclass floatClass = nullptr;
    jclass doubleClass = nullptr;
    jclass throwableClass = nullptr;

    jmethodID resolveClassMethod = nullptr;
    jmethodID getMemberMethod = nullptr;
    jmethodID invokeMethod = nullptr;
    jmethodID constructMethod = nullptr;
    jmethodID setMemberMethod = nullptr;
    jmethodID getIndexMethod = nullptr;
    jmethodID setIndexMethod = nullptr;
    jmethodID lengthMethod = nullptr;
    jmethodID objectsEqualMethod = nullptr;
    jmethodID objectToStringMethod = nullptr;

    jfieldID memberKindField = nullptr;
    jfieldID memberValueField = nullptr;
    jfieldID invocationHasValueField = nullptr;
    jfieldID invocationValueField = nullptr;

    jmethodID luaTableConstructor = nullptr;
    jmethodID luaTablePutMethod = nullptr;
    jmethodID luaCallbackConstructor = nullptr;

    jmethodID longValueOfMethod = nullptr;
    jmethodID doubleValueOfMethod = nullptr;
    jmethodID booleanValueOfMethod = nullptr;
    jmethodID numberLongValueMethod = nullptr;
    jmethodID numberDoubleValueMethod = nullptr;
    jmethodID booleanValueMethod = nullptr;
    jmethodID characterValueMethod = nullptr;
    jmethodID throwableToStringMethod = nullptr;
    jmethodID stringBytesConstructor = nullptr;
    jmethodID stringGetBytesMethod = nullptr;
};

JavaVM* gJavaVm = nullptr;
JavaInteropCache gJavaCache;
std::mutex gJavaCacheMutex;
std::atomic<std::uint64_t> gNextContextToken{1};
std::mutex gContextRegistryMutex;
std::unordered_map<std::uint64_t, std::shared_ptr<LuaJavaContext>> gContextRegistry;

/**
 * 获取当前线程 JNIEnv；native 创建的线程会在需要时附加到 JVM。
 */
JNIEnv* currentEnv(bool* attached = nullptr) {
    if (attached != nullptr) {
        *attached = false;
    }
    if (gJavaVm == nullptr) {
        return nullptr;
    }

    JNIEnv* env = nullptr;
    jint status = gJavaVm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (status == JNI_OK) {
        return env;
    }
    if (status == JNI_EDETACHED
            && gJavaVm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
        if (attached != nullptr) {
            *attached = true;
        }
        return env;
    }
    return nullptr;
}

/**
 * 将当前 Java 异常转换成文本并清除异常状态。
 */
std::string takeJavaException(JNIEnv* env) {
    if (env == nullptr || !env->ExceptionCheck()) {
        return "";
    }

    jthrowable throwable = env->ExceptionOccurred();
    env->ExceptionClear();
    if (throwable == nullptr) {
        return "Java 调用失败";
    }

    std::string message = "Java 调用失败";
    if (gJavaCache.throwableToStringMethod != nullptr) {
        jstring text = static_cast<jstring>(env->CallObjectMethod(
                throwable,
                gJavaCache.throwableToStringMethod
        ));
        if (!env->ExceptionCheck() && text != nullptr) {
            jstring charset = env->NewStringUTF("UTF-8");
            jbyteArray bytes = static_cast<jbyteArray>(env->CallObjectMethod(
                    text,
                    gJavaCache.stringGetBytesMethod,
                    charset
            ));
            if (!env->ExceptionCheck() && bytes != nullptr) {
                jsize length = env->GetArrayLength(bytes);
                message.resize(static_cast<size_t>(length));
                if (length > 0) {
                    env->GetByteArrayRegion(
                            bytes,
                            0,
                            length,
                            reinterpret_cast<jbyte*>(message.data())
                    );
                }
                env->DeleteLocalRef(bytes);
            }
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            if (charset != nullptr) {
                env->DeleteLocalRef(charset);
            }
            env->DeleteLocalRef(text);
        } else if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    }

    env->DeleteLocalRef(throwable);
    return message;
}

/**
 * 将 UTF-8 Lua 字符串无损转换为 Java String。
 */
jstring utf8ToJavaString(JNIEnv* env, const char* text, size_t length) {
    if (env == nullptr || text == nullptr) {
        return nullptr;
    }

    jbyteArray bytes = env->NewByteArray(static_cast<jsize>(length));
    if (bytes == nullptr) {
        return nullptr;
    }
    if (length > 0) {
        env->SetByteArrayRegion(
                bytes,
                0,
                static_cast<jsize>(length),
                reinterpret_cast<const jbyte*>(text)
        );
    }

    jstring charset = env->NewStringUTF("UTF-8");
    jstring result = static_cast<jstring>(env->NewObject(
            gJavaCache.stringClass,
            gJavaCache.stringBytesConstructor,
            bytes,
            charset
    ));
    env->DeleteLocalRef(bytes);
    if (charset != nullptr) {
        env->DeleteLocalRef(charset);
    }
    return result;
}

/**
 * 将 Java String 按标准 UTF-8 转换为 C++ 字符串。
 */
std::string javaStringToUtf8(JNIEnv* env, jstring text) {
    if (env == nullptr || text == nullptr) {
        return "";
    }

    jstring charset = env->NewStringUTF("UTF-8");
    jbyteArray bytes = static_cast<jbyteArray>(env->CallObjectMethod(
            text,
            gJavaCache.stringGetBytesMethod,
            charset
    ));
    if (charset != nullptr) {
        env->DeleteLocalRef(charset);
    }
    if (env->ExceptionCheck() || bytes == nullptr) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        return "";
    }

    jsize length = env->GetArrayLength(bytes);
    std::string result(static_cast<size_t>(length), '\0');
    if (length > 0) {
        env->GetByteArrayRegion(
                bytes,
                0,
                length,
                reinterpret_cast<jbyte*>(result.data())
        );
    }
    env->DeleteLocalRef(bytes);
    return result;
}

/**
 * 把局部类引用提升为进程级 GlobalRef。
 */
bool cacheClass(JNIEnv* env, const char* className, jclass* target) {
    jclass localClass = env->FindClass(className);
    if (localClass == nullptr) {
        return false;
    }
    *target = static_cast<jclass>(env->NewGlobalRef(localClass));
    env->DeleteLocalRef(localClass);
    return *target != nullptr;
}

/**
 * 初始化 Java 互操作类和反射入口缓存。
 */
void initializeJavaCache(JNIEnv* env) {
    std::lock_guard<std::mutex> lock(gJavaCacheMutex);
    if (gJavaCache.ready || env == nullptr) {
        return;
    }

    bool classesReady =
            cacheClass(env, "com/autolua/engine/interop/JavaInteropBridge", &gJavaCache.bridgeClass)
            && cacheClass(env, "com/autolua/engine/interop/JavaMemberResult", &gJavaCache.memberResultClass)
            && cacheClass(env, "com/autolua/engine/interop/JavaInvocationResult", &gJavaCache.invocationResultClass)
            && cacheClass(env, "com/autolua/engine/interop/LuaTableValue", &gJavaCache.luaTableClass)
            && cacheClass(env, "com/autolua/engine/interop/LuaCallback", &gJavaCache.luaCallbackClass)
            && cacheClass(env, "java/lang/Object", &gJavaCache.objectClass)
            && cacheClass(env, "java/lang/Class", &gJavaCache.classClass)
            && cacheClass(env, "java/lang/String", &gJavaCache.stringClass)
            && cacheClass(env, "java/lang/Number", &gJavaCache.numberClass)
            && cacheClass(env, "java/lang/Boolean", &gJavaCache.booleanClass)
            && cacheClass(env, "java/lang/Character", &gJavaCache.characterClass)
            && cacheClass(env, "java/lang/Byte", &gJavaCache.byteClass)
            && cacheClass(env, "java/lang/Short", &gJavaCache.shortClass)
            && cacheClass(env, "java/lang/Integer", &gJavaCache.integerClass)
            && cacheClass(env, "java/lang/Long", &gJavaCache.longClass)
            && cacheClass(env, "java/lang/Float", &gJavaCache.floatClass)
            && cacheClass(env, "java/lang/Double", &gJavaCache.doubleClass)
            && cacheClass(env, "java/lang/Throwable", &gJavaCache.throwableClass);
    if (!classesReady) {
        gJavaCache.error = takeJavaException(env);
        if (gJavaCache.error.empty()) {
            gJavaCache.error = "Java 互操作类加载失败";
        }
        return;
    }

    gJavaCache.resolveClassMethod = env->GetStaticMethodID(
            gJavaCache.bridgeClass,
            "resolveClass",
            "(Ljava/lang/String;)Ljava/lang/Class;"
    );
    gJavaCache.getMemberMethod = env->GetStaticMethodID(
            gJavaCache.bridgeClass,
            "getMember",
            "(Ljava/lang/Object;Ljava/lang/String;)Lcom/autolua/engine/interop/JavaMemberResult;"
    );
    gJavaCache.invokeMethod = env->GetStaticMethodID(
            gJavaCache.bridgeClass,
            "invoke",
            "(Ljava/lang/Object;Ljava/lang/String;[Ljava/lang/Object;)Lcom/autolua/engine/interop/JavaInvocationResult;"
    );
    gJavaCache.constructMethod = env->GetStaticMethodID(
            gJavaCache.bridgeClass,
            "construct",
            "(Ljava/lang/Class;[Ljava/lang/Object;)Lcom/autolua/engine/interop/JavaInvocationResult;"
    );
    gJavaCache.setMemberMethod = env->GetStaticMethodID(
            gJavaCache.bridgeClass,
            "setMember",
            "(Ljava/lang/Object;Ljava/lang/String;Ljava/lang/Object;)V"
    );
    gJavaCache.getIndexMethod = env->GetStaticMethodID(
            gJavaCache.bridgeClass,
            "getIndex",
            "(Ljava/lang/Object;Ljava/lang/Object;)Lcom/autolua/engine/interop/JavaInvocationResult;"
    );
    gJavaCache.setIndexMethod = env->GetStaticMethodID(
            gJavaCache.bridgeClass,
            "setIndex",
            "(Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;)V"
    );
    gJavaCache.lengthMethod = env->GetStaticMethodID(
            gJavaCache.bridgeClass,
            "length",
            "(Ljava/lang/Object;)I"
    );
    gJavaCache.objectsEqualMethod = env->GetStaticMethodID(
            gJavaCache.bridgeClass,
            "objectsEqual",
            "(Ljava/lang/Object;Ljava/lang/Object;)Z"
    );
    gJavaCache.objectToStringMethod = env->GetStaticMethodID(
            gJavaCache.bridgeClass,
            "objectToString",
            "(Ljava/lang/Object;)Ljava/lang/String;"
    );

    gJavaCache.memberKindField = env->GetFieldID(
            gJavaCache.memberResultClass,
            "kind",
            "I"
    );
    gJavaCache.memberValueField = env->GetFieldID(
            gJavaCache.memberResultClass,
            "value",
            "Ljava/lang/Object;"
    );
    gJavaCache.invocationHasValueField = env->GetFieldID(
            gJavaCache.invocationResultClass,
            "hasValue",
            "Z"
    );
    gJavaCache.invocationValueField = env->GetFieldID(
            gJavaCache.invocationResultClass,
            "value",
            "Ljava/lang/Object;"
    );

    gJavaCache.luaTableConstructor = env->GetMethodID(
            gJavaCache.luaTableClass,
            "<init>",
            "()V"
    );
    gJavaCache.luaTablePutMethod = env->GetMethodID(
            gJavaCache.luaTableClass,
            "put",
            "(Ljava/lang/Object;Ljava/lang/Object;)V"
    );
    gJavaCache.luaCallbackConstructor = env->GetMethodID(
            gJavaCache.luaCallbackClass,
            "<init>",
            "(JI)V"
    );

    gJavaCache.longValueOfMethod = env->GetStaticMethodID(
            gJavaCache.longClass,
            "valueOf",
            "(J)Ljava/lang/Long;"
    );
    gJavaCache.doubleValueOfMethod = env->GetStaticMethodID(
            gJavaCache.doubleClass,
            "valueOf",
            "(D)Ljava/lang/Double;"
    );
    gJavaCache.booleanValueOfMethod = env->GetStaticMethodID(
            gJavaCache.booleanClass,
            "valueOf",
            "(Z)Ljava/lang/Boolean;"
    );
    gJavaCache.numberLongValueMethod = env->GetMethodID(
            gJavaCache.numberClass,
            "longValue",
            "()J"
    );
    gJavaCache.numberDoubleValueMethod = env->GetMethodID(
            gJavaCache.numberClass,
            "doubleValue",
            "()D"
    );
    gJavaCache.booleanValueMethod = env->GetMethodID(
            gJavaCache.booleanClass,
            "booleanValue",
            "()Z"
    );
    gJavaCache.characterValueMethod = env->GetMethodID(
            gJavaCache.characterClass,
            "charValue",
            "()C"
    );
    gJavaCache.throwableToStringMethod = env->GetMethodID(
            gJavaCache.throwableClass,
            "toString",
            "()Ljava/lang/String;"
    );
    gJavaCache.stringBytesConstructor = env->GetMethodID(
            gJavaCache.stringClass,
            "<init>",
            "([BLjava/lang/String;)V"
    );
    gJavaCache.stringGetBytesMethod = env->GetMethodID(
            gJavaCache.stringClass,
            "getBytes",
            "(Ljava/lang/String;)[B"
    );

    if (env->ExceptionCheck()) {
        gJavaCache.error = takeJavaException(env);
        return;
    }

    bool methodsReady = gJavaCache.resolveClassMethod != nullptr
            && gJavaCache.getMemberMethod != nullptr
            && gJavaCache.invokeMethod != nullptr
            && gJavaCache.constructMethod != nullptr
            && gJavaCache.setMemberMethod != nullptr
            && gJavaCache.getIndexMethod != nullptr
            && gJavaCache.setIndexMethod != nullptr
            && gJavaCache.lengthMethod != nullptr
            && gJavaCache.objectsEqualMethod != nullptr
            && gJavaCache.objectToStringMethod != nullptr
            && gJavaCache.memberKindField != nullptr
            && gJavaCache.memberValueField != nullptr
            && gJavaCache.invocationHasValueField != nullptr
            && gJavaCache.invocationValueField != nullptr
            && gJavaCache.luaTableConstructor != nullptr
            && gJavaCache.luaTablePutMethod != nullptr
            && gJavaCache.luaCallbackConstructor != nullptr
            && gJavaCache.longValueOfMethod != nullptr
            && gJavaCache.doubleValueOfMethod != nullptr
            && gJavaCache.booleanValueOfMethod != nullptr
            && gJavaCache.numberLongValueMethod != nullptr
            && gJavaCache.numberDoubleValueMethod != nullptr
            && gJavaCache.booleanValueMethod != nullptr
            && gJavaCache.characterValueMethod != nullptr
            && gJavaCache.throwableToStringMethod != nullptr
            && gJavaCache.stringBytesConstructor != nullptr
            && gJavaCache.stringGetBytesMethod != nullptr;
    gJavaCache.ready = methodsReady;
    if (!methodsReady) {
        gJavaCache.error = "Java 互操作 JNI 方法缓存不完整";
    }
}

/**
 * 根据 token 取得仍然存活的 Lua Java 上下文。
 */
std::shared_ptr<LuaJavaContext> findContext(std::uint64_t token) {
    std::lock_guard<std::mutex> lock(gContextRegistryMutex);
    auto iterator = gContextRegistry.find(token);
    return iterator == gContextRegistry.end() ? nullptr : iterator->second;
}

/**
 * 从 Lua registry 读取当前运行时的上下文。
 */
std::shared_ptr<LuaJavaContext> contextForState(lua_State* state) {
    lua_getfield(state, LUA_REGISTRYINDEX, kJavaContextTokenKey);
    std::uint64_t token = static_cast<std::uint64_t>(lua_tointeger(state, -1));
    lua_pop(state, 1);
    return findContext(token);
}

/**
 * 在进入可能触发 Java 接口回调的调用前标记 Lua VM 已暂停在 JNI 边界。
 */
void beginJavaCall(const std::shared_ptr<LuaJavaContext>& context) {
    if (context == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(context->stateMutex);
    context->javaCallDepth++;
}

/**
 * Java 调用返回后，等待正在其他 Java 线程直接执行的同步 Lua 回调结束。
 */
void endJavaCall(const std::shared_ptr<LuaJavaContext>& context) {
    if (context == nullptr) {
        return;
    }

    std::unique_lock<std::mutex> lock(context->stateMutex);
    if (context->javaCallDepth > 0) {
        context->javaCallDepth--;
    }
    if (context->javaCallDepth == 0) {
        context->stateCondition.wait(lock, [&context]() {
            return context->activeDirectCallbacks == 0;
        });
    }
}

/**
 * 判断一个 Lua 值是否是本互操作层创建的 Java userdata。
 */
JavaObjectUserdata* testJavaObject(lua_State* state, int index) {
    return static_cast<JavaObjectUserdata*>(luaL_testudata(
            state,
            index,
            kJavaObjectMetatable
    ));
}

/**
 * 校验并返回 Java userdata。
 */
JavaObjectUserdata* checkJavaObject(lua_State* state, int index) {
    return static_cast<JavaObjectUserdata*>(luaL_checkudata(
            state,
            index,
            kJavaObjectMetatable
    ));
}

/**
 * 将任意 Java 对象包装成 Lua userdata；基础类型由 pushJavaValue 直接转换。
 */
int pushJavaUserdata(
        lua_State* state,
        JNIEnv* env,
        const std::shared_ptr<LuaJavaContext>& context,
        jobject value
) {
    auto* userdata = static_cast<JavaObjectUserdata*>(lua_newuserdatauv(
            state,
            sizeof(JavaObjectUserdata),
            0
    ));
    userdata->object = env->NewGlobalRef(value);
    userdata->contextToken = context == nullptr ? 0 : context->token;
    userdata->isClass = env->IsInstanceOf(value, gJavaCache.classClass) == JNI_TRUE;
    luaL_getmetatable(state, kJavaObjectMetatable);
    lua_setmetatable(state, -2);
    return 1;
}

/**
 * 把 Java 返回值压入 Lua；Java 对象保持 userdata，可继续读取字段和调用方法。
 */
int pushJavaValue(
        lua_State* state,
        JNIEnv* env,
        const std::shared_ptr<LuaJavaContext>& context,
        jobject value,
        std::string* error
) {
    if (value == nullptr) {
        lua_pushnil(state);
        return 1;
    }

    if (env->IsInstanceOf(value, gJavaCache.booleanClass)) {
        jboolean result = env->CallBooleanMethod(value, gJavaCache.booleanValueMethod);
        if (env->ExceptionCheck()) {
            *error = takeJavaException(env);
            return 0;
        }
        lua_pushboolean(state, result == JNI_TRUE);
        return 1;
    }

    if (env->IsInstanceOf(value, gJavaCache.stringClass)) {
        std::string text = javaStringToUtf8(env, static_cast<jstring>(value));
        lua_pushlstring(state, text.data(), text.size());
        return 1;
    }

    if (env->IsInstanceOf(value, gJavaCache.characterClass)) {
        jstring text = static_cast<jstring>(env->CallStaticObjectMethod(
                gJavaCache.bridgeClass,
                gJavaCache.objectToStringMethod,
                value
        ));
        if (env->ExceptionCheck()) {
            *error = takeJavaException(env);
            return 0;
        }
        std::string utf8 = javaStringToUtf8(env, text);
        lua_pushlstring(state, utf8.data(), utf8.size());
        if (text != nullptr) {
            env->DeleteLocalRef(text);
        }
        return 1;
    }

    bool integral = env->IsInstanceOf(value, gJavaCache.byteClass)
            || env->IsInstanceOf(value, gJavaCache.shortClass)
            || env->IsInstanceOf(value, gJavaCache.integerClass)
            || env->IsInstanceOf(value, gJavaCache.longClass);
    if (integral) {
        jlong number = env->CallLongMethod(value, gJavaCache.numberLongValueMethod);
        if (env->ExceptionCheck()) {
            *error = takeJavaException(env);
            return 0;
        }
        lua_pushinteger(state, static_cast<lua_Integer>(number));
        return 1;
    }

    if (env->IsInstanceOf(value, gJavaCache.numberClass)) {
        jdouble number = env->CallDoubleMethod(value, gJavaCache.numberDoubleValueMethod);
        if (env->ExceptionCheck()) {
            *error = takeJavaException(env);
            return 0;
        }
        lua_pushnumber(state, static_cast<lua_Number>(number));
        return 1;
    }

    return pushJavaUserdata(state, env, context, value);
}

/**
 * 记录一个 Lua 函数 registry 引用，统一在 LuaRuntime 销毁时释放。
 */
void trackCallbackReference(
        const std::shared_ptr<LuaJavaContext>& context,
        int reference
) {
    std::lock_guard<std::mutex> lock(context->stateMutex);
    context->callbackReferences.push_back(reference);
}

/**
 * 将 Lua 值转换为 Java 参数中间值。
 */
jobject luaToJavaValue(
        lua_State* state,
        int index,
        JNIEnv* env,
        const std::shared_ptr<LuaJavaContext>& context,
        std::unordered_set<const void*>* visitedTables,
        std::string* error
) {
    int type = lua_type(state, index);
    switch (type) {
        case LUA_TNIL:
            return nullptr;
        case LUA_TBOOLEAN:
            return env->CallStaticObjectMethod(
                    gJavaCache.booleanClass,
                    gJavaCache.booleanValueOfMethod,
                    static_cast<jboolean>(lua_toboolean(state, index) ? JNI_TRUE : JNI_FALSE)
            );
        case LUA_TNUMBER:
            if (lua_isinteger(state, index)) {
                return env->CallStaticObjectMethod(
                        gJavaCache.longClass,
                        gJavaCache.longValueOfMethod,
                        static_cast<jlong>(lua_tointeger(state, index))
                );
            }
            return env->CallStaticObjectMethod(
                    gJavaCache.doubleClass,
                    gJavaCache.doubleValueOfMethod,
                    static_cast<jdouble>(lua_tonumber(state, index))
            );
        case LUA_TSTRING: {
            size_t length = 0;
            const char* text = lua_tolstring(state, index, &length);
            return utf8ToJavaString(env, text, length);
        }
        case LUA_TFUNCTION: {
            lua_pushvalue(state, index);
            int reference = luaL_ref(state, LUA_REGISTRYINDEX);
            jobject callback = env->NewObject(
                    gJavaCache.luaCallbackClass,
                    gJavaCache.luaCallbackConstructor,
                    static_cast<jlong>(context->token),
                    static_cast<jint>(reference)
            );
            if (callback == nullptr || env->ExceptionCheck()) {
                luaL_unref(state, LUA_REGISTRYINDEX, reference);
                *error = takeJavaException(env);
                return nullptr;
            }
            trackCallbackReference(context, reference);
            return callback;
        }
        case LUA_TTABLE: {
            int absoluteIndex = lua_absindex(state, index);
            const void* tablePointer = lua_topointer(state, absoluteIndex);
            if (visitedTables->find(tablePointer) != visitedTables->end()) {
                *error = "Lua table 存在循环引用，不能转换为 Java 参数";
                return nullptr;
            }
            visitedTables->insert(tablePointer);

            jobject table = env->NewObject(
                    gJavaCache.luaTableClass,
                    gJavaCache.luaTableConstructor
            );
            if (table == nullptr || env->ExceptionCheck()) {
                visitedTables->erase(tablePointer);
                *error = takeJavaException(env);
                return nullptr;
            }

            int originalTop = lua_gettop(state);
            lua_pushnil(state);
            while (lua_next(state, absoluteIndex) != 0) {
                jobject key = luaToJavaValue(
                        state,
                        -2,
                        env,
                        context,
                        visitedTables,
                        error
                );
                if (!error->empty()) {
                    lua_settop(state, originalTop);
                    env->DeleteLocalRef(table);
                    visitedTables->erase(tablePointer);
                    return nullptr;
                }

                jobject value = luaToJavaValue(
                        state,
                        -1,
                        env,
                        context,
                        visitedTables,
                        error
                );
                if (!error->empty()) {
                    if (key != nullptr) {
                        env->DeleteLocalRef(key);
                    }
                    lua_settop(state, originalTop);
                    env->DeleteLocalRef(table);
                    visitedTables->erase(tablePointer);
                    return nullptr;
                }

                env->CallVoidMethod(table, gJavaCache.luaTablePutMethod, key, value);
                if (key != nullptr) {
                    env->DeleteLocalRef(key);
                }
                if (value != nullptr) {
                    env->DeleteLocalRef(value);
                }
                if (env->ExceptionCheck()) {
                    *error = takeJavaException(env);
                    lua_settop(state, originalTop);
                    env->DeleteLocalRef(table);
                    visitedTables->erase(tablePointer);
                    return nullptr;
                }
                lua_pop(state, 1);
            }

            visitedTables->erase(tablePointer);
            return table;
        }
        case LUA_TUSERDATA: {
            JavaObjectUserdata* userdata = testJavaObject(state, index);
            if (userdata == nullptr || userdata->object == nullptr) {
                *error = "当前 userdata 不是 Java 对象";
                return nullptr;
            }
            return env->NewLocalRef(userdata->object);
        }
        default:
            *error = std::string("Lua 类型不能转换为 Java 参数：")
                    + lua_typename(state, type);
            return nullptr;
    }
}

/**
 * 创建 Java Object[] 调用参数，并支持 `obj:method()` 时跳过重复 self。
 */
jobjectArray buildJavaArguments(
        lua_State* state,
        int firstArgument,
        JNIEnv* env,
        const std::shared_ptr<LuaJavaContext>& context,
        std::string* error
) {
    int argumentCount = lua_gettop(state) - firstArgument + 1;
    if (argumentCount < 0) {
        argumentCount = 0;
    }

    jobjectArray arguments = env->NewObjectArray(
            static_cast<jsize>(argumentCount),
            gJavaCache.objectClass,
            nullptr
    );
    if (arguments == nullptr || env->ExceptionCheck()) {
        *error = takeJavaException(env);
        return nullptr;
    }

    std::unordered_set<const void*> visitedTables;
    for (int offset = 0; offset < argumentCount; offset++) {
        jobject value = luaToJavaValue(
                state,
                firstArgument + offset,
                env,
                context,
                &visitedTables,
                error
        );
        if (!error->empty()) {
            env->DeleteLocalRef(arguments);
            return nullptr;
        }
        env->SetObjectArrayElement(arguments, offset, value);
        if (value != nullptr) {
            env->DeleteLocalRef(value);
        }
        if (env->ExceptionCheck()) {
            *error = takeJavaException(env);
            env->DeleteLocalRef(arguments);
            return nullptr;
        }
    }
    return arguments;
}

/**
 * 读取 JavaInvocationResult 并按 void/null 语义压入 Lua。
 */
int pushInvocationResult(
        lua_State* state,
        JNIEnv* env,
        const std::shared_ptr<LuaJavaContext>& context,
        jobject result,
        std::string* error
) {
    if (result == nullptr) {
        *error = "Java 调用没有返回 InvocationResult";
        return 0;
    }

    jboolean hasValue = env->GetBooleanField(
            result,
            gJavaCache.invocationHasValueField
    );
    if (env->ExceptionCheck()) {
        *error = takeJavaException(env);
        return 0;
    }
    if (hasValue != JNI_TRUE) {
        return 0;
    }

    jobject value = env->GetObjectField(result, gJavaCache.invocationValueField);
    int pushed = pushJavaValue(state, env, context, value, error);
    if (value != nullptr) {
        env->DeleteLocalRef(value);
    }
    return pushed;
}

/**
 * 判断 Lua 方法调用的首参数是否是已经绑定的同一个 Java 对象。
 */
bool firstArgumentIsBoundTarget(
        lua_State* state,
        JNIEnv* env,
        JavaObjectUserdata* target
) {
    if (lua_gettop(state) < 1 || target == nullptr) {
        return false;
    }
    JavaObjectUserdata* first = testJavaObject(state, 1);
    return first != nullptr
            && first->object != nullptr
            && env->IsSameObject(first->object, target->object) == JNI_TRUE;
}

/**
 * 调用一个已经保存到 Lua registry 的回调函数。
 */
jobject invokeLuaCallback(
        const std::shared_ptr<LuaJavaContext>& context,
        JNIEnv* env,
        int reference,
        const std::vector<jobject>& arguments,
        std::string* error
) {
    if (context == nullptr || !context->alive || context->state == nullptr) {
        *error = "LuaRuntime 已结束，Java 回调不可用";
        return nullptr;
    }

    lua_State* state = context->state;
    int originalTop = lua_gettop(state);
    context->callbackDepth.fetch_add(1);

    lua_rawgeti(state, LUA_REGISTRYINDEX, reference);
    if (!lua_isfunction(state, -1)) {
        lua_settop(state, originalTop);
        context->callbackDepth.fetch_sub(1);
        *error = "Lua 回调引用已失效";
        return nullptr;
    }

    for (jobject argument : arguments) {
        if (pushJavaValue(state, env, context, argument, error) == 0) {
            lua_settop(state, originalTop);
            context->callbackDepth.fetch_sub(1);
            return nullptr;
        }
    }

    int callStatus = lua_pcall(
            state,
            static_cast<int>(arguments.size()),
            1,
            0
    );
    if (callStatus != LUA_OK) {
        const char* message = lua_tostring(state, -1);
        *error = message == nullptr ? "Lua 接口回调执行失败" : message;
        lua_settop(state, originalTop);
        context->callbackDepth.fetch_sub(1);
        return nullptr;
    }

    std::unordered_set<const void*> visitedTables;
    jobject result = luaToJavaValue(
            state,
            -1,
            env,
            context,
            &visitedTables,
            error
    );
    lua_settop(state, originalTop);
    context->callbackDepth.fetch_sub(1);
    return result;
}

/**
 * 删除一次回调请求持有的 Java GlobalRef。
 */
void releaseCallbackRequestRefs(JNIEnv* env, LuaCallbackRequest* request) {
    if (env == nullptr || request == nullptr) {
        return;
    }
    for (jobject argument : request->arguments) {
        if (argument != nullptr) {
            env->DeleteGlobalRef(argument);
        }
    }
    request->arguments.clear();
}

/**
 * 执行一个已排队回调，并把结果唤醒给等待的 Java 线程。
 */
void executeQueuedCallback(
        const std::shared_ptr<LuaJavaContext>& context,
        JNIEnv* env,
        const std::shared_ptr<LuaCallbackRequest>& request
) {
    std::string error;
    jobject localResult = nullptr;
    {
        std::lock_guard<std::recursive_mutex> callbackLock(context->luaCallbackMutex);
        localResult = invokeLuaCallback(
                context,
                env,
                request->reference,
                request->arguments,
                &error
        );
    }

    jobject globalResult = localResult == nullptr ? nullptr : env->NewGlobalRef(localResult);
    if (localResult != nullptr) {
        env->DeleteLocalRef(localResult);
    }
    releaseCallbackRequestRefs(env, request.get());

    {
        std::lock_guard<std::mutex> lock(request->mutex);
        request->result = globalResult;
        request->error = error;
        request->done = true;
    }
    request->condition.notify_all();

    if (!request->waitForResult) {
        if (!error.empty()) {
            std::string logMessage = "Java 接口回调失败：" + error;
            engine_print(logMessage.c_str());
        }
        if (globalResult != nullptr) {
            env->DeleteGlobalRef(globalResult);
            std::lock_guard<std::mutex> lock(request->mutex);
            request->result = nullptr;
        }
    }
}

/**
 * 以 Lua 错误结束当前 C 函数。
 */
int raiseLuaError(lua_State* state, const std::string& message) {
    return luaL_error(state, "%s", message.empty() ? "Java 互操作调用失败" : message.c_str());
}

/**
 * import 完整类名时提取写入 _G 的简单类名。
 */
std::string simpleClassName(const std::string& className) {
    size_t position = className.find_last_of(".$");
    return position == std::string::npos ? className : className.substr(position + 1);
}

/**
 * 调用 Java 类解析器；quiet=true 时用于通配包试探，不保留 ClassNotFound 异常。
 */
jobject resolveJavaClass(
        JNIEnv* env,
        const std::shared_ptr<LuaJavaContext>& context,
        const std::string& className,
        bool quiet,
        std::string* error
) {
    jstring javaName = utf8ToJavaString(env, className.data(), className.size());
    beginJavaCall(context);
    jobject result = env->CallStaticObjectMethod(
            gJavaCache.bridgeClass,
            gJavaCache.resolveClassMethod,
            javaName
    );
    std::string javaError = takeJavaException(env);
    endJavaCall(context);
    if (javaName != nullptr) {
        env->DeleteLocalRef(javaName);
    }

    if (!javaError.empty() && !quiet) {
        *error = javaError;
    }
    return javaError.empty() ? result : nullptr;
}

/**
 * Lua 全局 import(className) 实现。
 */
int luaJavaImport(lua_State* state) {
    size_t length = 0;
    const char* classNameText = luaL_checklstring(state, 1, &length);
    std::string className(classNameText, length);
    std::shared_ptr<LuaJavaContext> context = contextForState(state);
    if (context == nullptr || !context->alive) {
        return raiseLuaError(state, "Lua Java 互操作上下文不可用");
    }
    if (!gJavaCache.ready) {
        return raiseLuaError(state, gJavaCache.error);
    }

    if (className.size() >= 2
            && className.compare(className.size() - 2, 2, ".*") == 0) {
        std::string packageName = className.substr(0, className.size() - 1);
        bool exists = false;
        for (const std::string& imported : context->importedPackages) {
            if (imported == packageName) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            context->importedPackages.push_back(packageName);
        }
        return 0;
    }

    JNIEnv* env = currentEnv();
    if (env == nullptr) {
        return raiseLuaError(state, "当前线程无法访问 JavaVM");
    }

    std::string error;
    jobject javaClass = resolveJavaClass(env, context, className, false, &error);
    if (javaClass == nullptr) {
        return raiseLuaError(state, error);
    }

    std::string alias = simpleClassName(className);
    pushJavaUserdata(state, env, context, javaClass);
    lua_setglobal(state, alias.c_str());
    env->DeleteLocalRef(javaClass);
    return 0;
}

/**
 * _G 缺失字段回调：按 import('package.*') 的顺序延迟解析简单类名。
 */
int luaJavaGlobalIndex(lua_State* state) {
    std::shared_ptr<LuaJavaContext> context = contextForState(state);
    if (context == nullptr || !context->alive || !lua_isstring(state, 2)) {
        lua_pushnil(state);
        return 1;
    }

    size_t keyLength = 0;
    const char* keyText = lua_tolstring(state, 2, &keyLength);
    std::string key(keyText, keyLength);
    JNIEnv* env = currentEnv();
    if (env != nullptr && gJavaCache.ready) {
        for (const std::string& packageName : context->importedPackages) {
            std::string fullName = packageName + key;
            std::string ignoredError;
            jobject javaClass = resolveJavaClass(
                    env,
                    context,
                    fullName,
                    true,
                    &ignoredError
            );
            if (javaClass == nullptr) {
                continue;
            }

            pushJavaUserdata(state, env, context, javaClass);
            lua_pushvalue(state, 2);
            lua_pushvalue(state, -2);
            lua_rawset(state, 1);
            env->DeleteLocalRef(javaClass);
            return 1;
        }
    }

    if (context->previousGlobalIndexReference != LUA_NOREF) {
        lua_rawgeti(
                state,
                LUA_REGISTRYINDEX,
                context->previousGlobalIndexReference
        );
        if (lua_isfunction(state, -1)) {
            lua_pushvalue(state, 1);
            lua_pushvalue(state, 2);
            lua_call(state, 2, 1);
            return 1;
        }
        if (lua_istable(state, -1)) {
            lua_pushvalue(state, 2);
            lua_gettable(state, -2);
            lua_remove(state, -2);
            return 1;
        }
        lua_pop(state, 1);
    }

    lua_pushnil(state);
    return 1;
}

/**
 * Java userdata 的 __index：字段直接返回，方法返回绑定目标的闭包。
 */
int luaJavaObjectIndex(lua_State* state) {
    JavaObjectUserdata* target = checkJavaObject(state, 1);
    std::shared_ptr<LuaJavaContext> context = findContext(target->contextToken);
    if (context == nullptr || !context->alive) {
        return raiseLuaError(state, "Java 对象所属 LuaRuntime 已结束");
    }

    JNIEnv* env = currentEnv();
    if (env == nullptr) {
        return raiseLuaError(state, "当前线程无法访问 JavaVM");
    }

    if (lua_type(state, 2) != LUA_TSTRING) {
        std::unordered_set<const void*> visitedTables;
        std::string error;
        jobject key = luaToJavaValue(
                state,
                2,
                env,
                context,
                &visitedTables,
                &error
        );
        if (!error.empty()) {
            return raiseLuaError(state, error);
        }

        beginJavaCall(context);
        jobject result = env->CallStaticObjectMethod(
                gJavaCache.bridgeClass,
                gJavaCache.getIndexMethod,
                target->object,
                key
        );
        std::string javaError = takeJavaException(env);
        endJavaCall(context);
        if (key != nullptr) {
            env->DeleteLocalRef(key);
        }

        int pushed = 0;
        if (javaError.empty()) {
            pushed = pushInvocationResult(state, env, context, result, &javaError);
        }
        if (result != nullptr) {
            env->DeleteLocalRef(result);
        }
        return javaError.empty() ? pushed : raiseLuaError(state, javaError);
    }

    size_t nameLength = 0;
    const char* nameText = lua_tolstring(state, 2, &nameLength);
    jstring memberName = utf8ToJavaString(env, nameText, nameLength);
    beginJavaCall(context);
    jobject memberResult = env->CallStaticObjectMethod(
            gJavaCache.bridgeClass,
            gJavaCache.getMemberMethod,
            target->object,
            memberName
    );
    std::string error = takeJavaException(env);
    endJavaCall(context);
    if (memberName != nullptr) {
        env->DeleteLocalRef(memberName);
    }
    if (!error.empty()) {
        if (memberResult != nullptr) {
            env->DeleteLocalRef(memberResult);
        }
        return raiseLuaError(state, error);
    }
    if (memberResult == nullptr) {
        lua_pushnil(state);
        return 1;
    }

    jint kind = env->GetIntField(memberResult, gJavaCache.memberKindField);
    if (env->ExceptionCheck()) {
        error = takeJavaException(env);
        env->DeleteLocalRef(memberResult);
        return raiseLuaError(state, error);
    }

    if (kind == 1) {
        jobject value = env->GetObjectField(memberResult, gJavaCache.memberValueField);
        int pushed = pushJavaValue(state, env, context, value, &error);
        if (value != nullptr) {
            env->DeleteLocalRef(value);
        }
        env->DeleteLocalRef(memberResult);
        return error.empty() ? pushed : raiseLuaError(state, error);
    }

    if (kind == 2) {
        lua_pushvalue(state, 1);
        lua_pushlstring(state, nameText, nameLength);
        lua_pushcclosure(state, [](lua_State* callState) -> int {
            JavaObjectUserdata* boundTarget = checkJavaObject(
                    callState,
                    lua_upvalueindex(1)
            );
            size_t methodLength = 0;
            const char* methodText = lua_tolstring(
                    callState,
                    lua_upvalueindex(2),
                    &methodLength
            );
            std::shared_ptr<LuaJavaContext> callContext = findContext(
                    boundTarget->contextToken
            );
            if (callContext == nullptr || !callContext->alive) {
                return raiseLuaError(callState, "Java 对象所属 LuaRuntime 已结束");
            }

            JNIEnv* callEnv = currentEnv();
            if (callEnv == nullptr) {
                return raiseLuaError(callState, "当前线程无法访问 JavaVM");
            }

            int firstArgument = firstArgumentIsBoundTarget(
                    callState,
                    callEnv,
                    boundTarget
            ) ? 2 : 1;
            std::string callError;
            jobjectArray arguments = buildJavaArguments(
                    callState,
                    firstArgument,
                    callEnv,
                    callContext,
                    &callError
            );
            if (!callError.empty()) {
                return raiseLuaError(callState, callError);
            }

            jstring methodName = utf8ToJavaString(
                    callEnv,
                    methodText,
                    methodLength
            );
            beginJavaCall(callContext);
            jobject invocationResult = callEnv->CallStaticObjectMethod(
                    gJavaCache.bridgeClass,
                    gJavaCache.invokeMethod,
                    boundTarget->object,
                    methodName,
                    arguments
            );
            std::string javaError = takeJavaException(callEnv);
            endJavaCall(callContext);

            if (methodName != nullptr) {
                callEnv->DeleteLocalRef(methodName);
            }
            if (arguments != nullptr) {
                callEnv->DeleteLocalRef(arguments);
            }

            int pushed = 0;
            if (javaError.empty()) {
                pushed = pushInvocationResult(
                        callState,
                        callEnv,
                        callContext,
                        invocationResult,
                        &javaError
                );
            }
            if (invocationResult != nullptr) {
                callEnv->DeleteLocalRef(invocationResult);
            }
            return javaError.empty() ? pushed : raiseLuaError(callState, javaError);
        }, 2);
        env->DeleteLocalRef(memberResult);
        return 1;
    }

    env->DeleteLocalRef(memberResult);
    lua_pushnil(state);
    return 1;
}

/**
 * Java userdata 的 __newindex：数字键写容器，字符串键写字段或 Map。
 */
int luaJavaObjectNewIndex(lua_State* state) {
    JavaObjectUserdata* target = checkJavaObject(state, 1);
    std::shared_ptr<LuaJavaContext> context = findContext(target->contextToken);
    if (context == nullptr || !context->alive) {
        return raiseLuaError(state, "Java 对象所属 LuaRuntime 已结束");
    }

    JNIEnv* env = currentEnv();
    if (env == nullptr) {
        return raiseLuaError(state, "当前线程无法访问 JavaVM");
    }

    std::unordered_set<const void*> visitedTables;
    std::string error;
    jobject value = luaToJavaValue(
            state,
            3,
            env,
            context,
            &visitedTables,
            &error
    );
    if (!error.empty()) {
        return raiseLuaError(state, error);
    }

    if (lua_type(state, 2) == LUA_TSTRING) {
        size_t nameLength = 0;
        const char* nameText = lua_tolstring(state, 2, &nameLength);
        jstring memberName = utf8ToJavaString(env, nameText, nameLength);
        beginJavaCall(context);
        env->CallStaticVoidMethod(
                gJavaCache.bridgeClass,
                gJavaCache.setMemberMethod,
                target->object,
                memberName,
                value
        );
        error = takeJavaException(env);
        endJavaCall(context);
        if (memberName != nullptr) {
            env->DeleteLocalRef(memberName);
        }
    } else {
        jobject key = luaToJavaValue(
                state,
                2,
                env,
                context,
                &visitedTables,
                &error
        );
        if (error.empty()) {
            beginJavaCall(context);
            env->CallStaticVoidMethod(
                    gJavaCache.bridgeClass,
                    gJavaCache.setIndexMethod,
                    target->object,
                    key,
                    value
            );
            error = takeJavaException(env);
            endJavaCall(context);
        }
        if (key != nullptr) {
            env->DeleteLocalRef(key);
        }
    }

    if (value != nullptr) {
        env->DeleteLocalRef(value);
    }
    return error.empty() ? 0 : raiseLuaError(state, error);
}

/**
 * Java Class userdata 的 __call：调用构造函数或创建接口代理/数组。
 */
int luaJavaObjectCall(lua_State* state) {
    JavaObjectUserdata* target = checkJavaObject(state, 1);
    if (!target->isClass) {
        return raiseLuaError(state, "当前 Java 对象不是可构造的 Class");
    }

    std::shared_ptr<LuaJavaContext> context = findContext(target->contextToken);
    if (context == nullptr || !context->alive) {
        return raiseLuaError(state, "Java Class 所属 LuaRuntime 已结束");
    }

    JNIEnv* env = currentEnv();
    if (env == nullptr) {
        return raiseLuaError(state, "当前线程无法访问 JavaVM");
    }

    std::string error;
    jobjectArray arguments = buildJavaArguments(state, 2, env, context, &error);
    if (!error.empty()) {
        return raiseLuaError(state, error);
    }

    beginJavaCall(context);
    jobject result = env->CallStaticObjectMethod(
            gJavaCache.bridgeClass,
            gJavaCache.constructMethod,
            target->object,
            arguments
    );
    std::string javaError = takeJavaException(env);
    endJavaCall(context);
    if (arguments != nullptr) {
        env->DeleteLocalRef(arguments);
    }

    int pushed = 0;
    if (javaError.empty()) {
        pushed = pushInvocationResult(state, env, context, result, &javaError);
    }
    if (result != nullptr) {
        env->DeleteLocalRef(result);
    }
    return javaError.empty() ? pushed : raiseLuaError(state, javaError);
}

/**
 * Java userdata 的 __len。
 */
int luaJavaObjectLength(lua_State* state) {
    JavaObjectUserdata* target = checkJavaObject(state, 1);
    std::shared_ptr<LuaJavaContext> context = findContext(target->contextToken);
    JNIEnv* env = currentEnv();
    if (context == nullptr || env == nullptr) {
        return raiseLuaError(state, "Java 对象上下文不可用");
    }

    beginJavaCall(context);
    jint length = env->CallStaticIntMethod(
            gJavaCache.bridgeClass,
            gJavaCache.lengthMethod,
            target->object
    );
    std::string error = takeJavaException(env);
    endJavaCall(context);
    if (!error.empty()) {
        return raiseLuaError(state, error);
    }
    lua_pushinteger(state, static_cast<lua_Integer>(length));
    return 1;
}

/**
 * Java userdata 的 __eq。
 */
int luaJavaObjectEqual(lua_State* state) {
    JavaObjectUserdata* left = testJavaObject(state, 1);
    JavaObjectUserdata* right = testJavaObject(state, 2);
    if (left == nullptr || right == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }

    std::shared_ptr<LuaJavaContext> context = findContext(left->contextToken);
    JNIEnv* env = currentEnv();
    if (context == nullptr || env == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }

    beginJavaCall(context);
    jboolean equal = env->CallStaticBooleanMethod(
            gJavaCache.bridgeClass,
            gJavaCache.objectsEqualMethod,
            left->object,
            right->object
    );
    std::string error = takeJavaException(env);
    endJavaCall(context);
    if (!error.empty()) {
        return raiseLuaError(state, error);
    }
    lua_pushboolean(state, equal == JNI_TRUE);
    return 1;
}

/**
 * Java userdata 的 __tostring。
 */
int luaJavaObjectToString(lua_State* state) {
    JavaObjectUserdata* target = checkJavaObject(state, 1);
    std::shared_ptr<LuaJavaContext> context = findContext(target->contextToken);
    JNIEnv* env = currentEnv();
    if (context == nullptr || env == nullptr) {
        lua_pushliteral(state, "JavaObject(已失效)");
        return 1;
    }

    beginJavaCall(context);
    jstring text = static_cast<jstring>(env->CallStaticObjectMethod(
            gJavaCache.bridgeClass,
            gJavaCache.objectToStringMethod,
            target->object
    ));
    std::string error = takeJavaException(env);
    endJavaCall(context);
    if (!error.empty()) {
        return raiseLuaError(state, error);
    }

    std::string utf8 = javaStringToUtf8(env, text);
    lua_pushlstring(state, utf8.data(), utf8.size());
    if (text != nullptr) {
        env->DeleteLocalRef(text);
    }
    return 1;
}

/**
 * Java userdata 的 __gc，释放 JNI GlobalRef。
 */
int luaJavaObjectGc(lua_State* state) {
    JavaObjectUserdata* target = checkJavaObject(state, 1);
    if (target->object != nullptr) {
        JNIEnv* env = currentEnv();
        if (env != nullptr) {
            env->DeleteGlobalRef(target->object);
        }
        target->object = nullptr;
    }
    return 0;
}

/**
 * 注册 Java userdata 元表。
 */
void registerJavaObjectMetatable(lua_State* state) {
    if (luaL_newmetatable(state, kJavaObjectMetatable) == 0) {
        lua_pop(state, 1);
        return;
    }

    lua_pushcfunction(state, luaJavaObjectIndex);
    lua_setfield(state, -2, "__index");
    lua_pushcfunction(state, luaJavaObjectNewIndex);
    lua_setfield(state, -2, "__newindex");
    lua_pushcfunction(state, luaJavaObjectCall);
    lua_setfield(state, -2, "__call");
    lua_pushcfunction(state, luaJavaObjectLength);
    lua_setfield(state, -2, "__len");
    lua_pushcfunction(state, luaJavaObjectEqual);
    lua_setfield(state, -2, "__eq");
    lua_pushcfunction(state, luaJavaObjectToString);
    lua_setfield(state, -2, "__tostring");
    lua_pushcfunction(state, luaJavaObjectGc);
    lua_setfield(state, -2, "__gc");
    lua_pushliteral(state, "JavaObject");
    lua_setfield(state, -2, "__name");
    lua_pop(state, 1);
}

/**
 * 安装 _G 的延迟 Java 类解析器，并保留已有 __index 行为。
 */
void installGlobalJavaIndex(
        lua_State* state,
        const std::shared_ptr<LuaJavaContext>& context
) {
    lua_pushglobaltable(state);
    if (!lua_getmetatable(state, -1)) {
        lua_newtable(state);
    }

    lua_getfield(state, -1, "__index");
    if (!lua_isnil(state, -1)) {
        context->previousGlobalIndexReference = luaL_ref(state, LUA_REGISTRYINDEX);
    } else {
        lua_pop(state, 1);
    }

    lua_pushcfunction(state, luaJavaGlobalIndex);
    lua_setfield(state, -2, "__index");
    lua_setmetatable(state, -2);
    lua_pop(state, 1);
}

} // namespace

void initializeLuaJavaBridge(JavaVM* javaVm) {
    gJavaVm = javaVm;
    JNIEnv* env = currentEnv();
    initializeJavaCache(env);
}

void registerLuaJavaBridge(lua_State* state, LuaRuntime* runtime) {
    if (state == nullptr) {
        return;
    }

    std::shared_ptr<LuaJavaContext> context = std::make_shared<LuaJavaContext>();
    context->state = state;
    context->runtime = runtime;
    context->token = gNextContextToken.fetch_add(1);
    context->ownerThread = std::this_thread::get_id();

    {
        std::lock_guard<std::mutex> lock(gContextRegistryMutex);
        gContextRegistry[context->token] = context;
    }

    lua_pushinteger(state, static_cast<lua_Integer>(context->token));
    lua_setfield(state, LUA_REGISTRYINDEX, kJavaContextTokenKey);
    registerJavaObjectMetatable(state);
    installGlobalJavaIndex(state, context);

    lua_pushcfunction(state, luaJavaImport);
    lua_setglobal(state, "import");
}

void unregisterLuaJavaBridge(lua_State* state) {
    if (state == nullptr) {
        return;
    }

    std::shared_ptr<LuaJavaContext> context = contextForState(state);
    if (context == nullptr) {
        return;
    }

    JNIEnv* env = currentEnv();
    {
        std::unique_lock<std::mutex> lock(context->stateMutex);
        context->alive.store(false);
        context->stateCondition.wait(lock, [&context]() {
            return context->activeDirectCallbacks == 0;
        });
    }

    std::deque<std::shared_ptr<LuaCallbackRequest>> pending;
    {
        std::lock_guard<std::mutex> lock(context->callbackQueueMutex);
        pending.swap(context->callbackQueue);
    }
    for (const std::shared_ptr<LuaCallbackRequest>& request : pending) {
        releaseCallbackRequestRefs(env, request.get());
        {
            std::lock_guard<std::mutex> lock(request->mutex);
            request->error = "LuaRuntime 已结束，Java 回调已取消";
            request->done = true;
        }
        request->condition.notify_all();
    }

    for (int reference : context->callbackReferences) {
        luaL_unref(state, LUA_REGISTRYINDEX, reference);
    }
    context->callbackReferences.clear();
    if (context->previousGlobalIndexReference != LUA_NOREF) {
        luaL_unref(
                state,
                LUA_REGISTRYINDEX,
                context->previousGlobalIndexReference
        );
        context->previousGlobalIndexReference = LUA_NOREF;
    }

    {
        std::lock_guard<std::mutex> lock(gContextRegistryMutex);
        gContextRegistry.erase(context->token);
    }
    lua_pushnil(state);
    lua_setfield(state, LUA_REGISTRYINDEX, kJavaContextTokenKey);
}

void processLuaJavaCallbacks(lua_State* state) {
    std::shared_ptr<LuaJavaContext> context = contextForState(state);
    if (context == nullptr
            || !context->alive
            || std::this_thread::get_id() != context->ownerThread
            || context->callbackDepth.load() > 0
            || context->processingCallbacks) {
        return;
    }

    JNIEnv* env = currentEnv();
    if (env == nullptr) {
        return;
    }

    context->processingCallbacks = true;
    while (true) {
        std::shared_ptr<LuaCallbackRequest> request;
        {
            std::lock_guard<std::mutex> lock(context->callbackQueueMutex);
            if (context->callbackQueue.empty()) {
                break;
            }
            request = context->callbackQueue.front();
            context->callbackQueue.pop_front();
        }
        executeQueuedCallback(context, env, request);
    }
    context->processingCallbacks = false;
}

/**
 * Java 动态代理回调 native 入口。
 *
 * 同一脚本线程或 Lua 已暂停在 Java 调用中时直接执行；其他时刻排队给脚本线程，
 * 从根本上避免多个线程同时访问 lua_State。
 */
extern "C" JNIEXPORT jobject JNICALL
Java_com_autolua_engine_interop_LuaCallback_nativeInvoke(
        JNIEnv* env,
        jclass clazz,
        jlong runtimeToken,
        jint reference,
        jobjectArray arguments,
        jboolean waitForResult
) {
    (void) clazz;
    std::shared_ptr<LuaJavaContext> context = findContext(
            static_cast<std::uint64_t>(runtimeToken)
    );
    if (context == nullptr || !context->alive) {
        jclass exceptionClass = env->FindClass("java/lang/IllegalStateException");
        env->ThrowNew(exceptionClass, "LuaRuntime 已结束，Java 回调不可用");
        if (exceptionClass != nullptr) {
            env->DeleteLocalRef(exceptionClass);
        }
        return nullptr;
    }

    std::vector<jobject> localArguments;
    jsize argumentCount = arguments == nullptr ? 0 : env->GetArrayLength(arguments);
    localArguments.reserve(static_cast<size_t>(argumentCount));
    for (jsize index = 0; index < argumentCount; index++) {
        localArguments.push_back(env->GetObjectArrayElement(arguments, index));
    }

    bool sameOwnerThread = std::this_thread::get_id() == context->ownerThread;
    bool directFromPausedJavaCall = false;
    if (!sameOwnerThread) {
        std::lock_guard<std::mutex> lock(context->stateMutex);
        if (context->alive && context->javaCallDepth > 0) {
            context->activeDirectCallbacks++;
            directFromPausedJavaCall = true;
        }
    }

    if (sameOwnerThread || directFromPausedJavaCall) {
        std::string error;
        jobject result = nullptr;
        {
            std::lock_guard<std::recursive_mutex> callbackLock(context->luaCallbackMutex);
            result = invokeLuaCallback(
                    context,
                    env,
                    static_cast<int>(reference),
                    localArguments,
                    &error
            );
        }
        for (jobject argument : localArguments) {
            if (argument != nullptr) {
                env->DeleteLocalRef(argument);
            }
        }

        if (directFromPausedJavaCall) {
            std::lock_guard<std::mutex> lock(context->stateMutex);
            context->activeDirectCallbacks--;
            context->stateCondition.notify_all();
        }
        if (!error.empty()) {
            jclass exceptionClass = env->FindClass("java/lang/RuntimeException");
            env->ThrowNew(exceptionClass, error.c_str());
            env->DeleteLocalRef(exceptionClass);
            if (result != nullptr) {
                env->DeleteLocalRef(result);
            }
            return nullptr;
        }
        return result;
    }

    std::shared_ptr<LuaCallbackRequest> request = std::make_shared<LuaCallbackRequest>();
    request->reference = static_cast<int>(reference);
    request->waitForResult = waitForResult == JNI_TRUE;
    request->arguments.reserve(localArguments.size());
    for (jobject argument : localArguments) {
        request->arguments.push_back(
                argument == nullptr ? nullptr : env->NewGlobalRef(argument)
        );
        if (argument != nullptr) {
            env->DeleteLocalRef(argument);
        }
    }

    {
        std::lock_guard<std::mutex> lock(context->callbackQueueMutex);
        if (!context->alive.load()) {
            releaseCallbackRequestRefs(env, request.get());
            jclass exceptionClass = env->FindClass("java/lang/IllegalStateException");
            env->ThrowNew(exceptionClass, "LuaRuntime 已结束，Java 回调不可用");
            if (exceptionClass != nullptr) {
                env->DeleteLocalRef(exceptionClass);
            }
            return nullptr;
        }
        context->callbackQueue.push_back(request);
    }

    if (!request->waitForResult) {
        return nullptr;
    }

    std::unique_lock<std::mutex> requestLock(request->mutex);
    request->condition.wait(requestLock, [&request, &context]() {
        return request->done || !context->alive;
    });
    if (!request->error.empty()) {
        std::string error = request->error;
        requestLock.unlock();
        jclass exceptionClass = env->FindClass("java/lang/RuntimeException");
        env->ThrowNew(exceptionClass, error.c_str());
        env->DeleteLocalRef(exceptionClass);
        return nullptr;
    }

    jobject result = request->result == nullptr
            ? nullptr
            : env->NewLocalRef(request->result);
    if (request->result != nullptr) {
        env->DeleteGlobalRef(request->result);
        request->result = nullptr;
    }
    return result;
}
