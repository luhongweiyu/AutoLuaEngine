package com.autolua.engine;

import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Java 层统一 native 入口。
 *
 * 后续所有 Java/Kotlin 到 C++ 引擎的调用都从这里走，避免 JNI 方法散落在各处。
 */
public final class NativeEngine {
    private static final AtomicBoolean RUNNING = new AtomicBoolean(false);

    private static boolean initialized;

    static {
        System.loadLibrary("engine");
    }

    private NativeEngine() {
    }

    public static synchronized void init() {
        if (initialized) {
            return;
        }
        nativeInit();
        initialized = true;
    }

    public static String runLuaText(String code) {
        if (!RUNNING.compareAndSet(false, true)) {
            return "Engine is already running";
        }

        try {
            return nativeRunLuaText(code);
        } finally {
            RUNNING.set(false);
        }
    }

    public static void stop() {
        nativeStop();
    }

    public static String drainLogs(int afterId) {
        return nativeDrainLogs(afterId);
    }

    public static String statusJson(int taskId) {
        return nativeStatusJson(taskId);
    }

    public static String engineVersion() {
        return nativeEngineVersion();
    }

    public static String luaVersion() {
        return nativeLuaVersion();
    }

    public static String captureScreenJson() {
        return nativeCaptureScreenJson();
    }

    public static boolean releaseImage(int imageId) {
        return nativeReleaseImage(imageId);
    }

    private static native void nativeInit();

    private static native String nativeRunLuaText(String code);

    private static native void nativeStop();

    private static native String nativeDrainLogs(int afterId);

    private static native String nativeStatusJson(int taskId);

    private static native String nativeEngineVersion();

    private static native String nativeLuaVersion();

    private static native String nativeCaptureScreenJson();

    private static native boolean nativeReleaseImage(int imageId);
}
