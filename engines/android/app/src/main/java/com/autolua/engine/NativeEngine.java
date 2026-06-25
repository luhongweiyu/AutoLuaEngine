package com.autolua.engine;

import android.content.Context;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Java 层统一 native 入口。
 *
 * 后续所有 Java/Kotlin 到 C++ 引擎的调用都从这里走，避免 JNI 方法散落在各处。
 */
public final class NativeEngine {
    private static final AtomicBoolean RUNNING = new AtomicBoolean(false);
    private static final String[] LUA_RUNTIME_ASSETS = {
            "runtime/api_m.lua",
            "runtime/compat_lr.lua",
            "runtime/compat_cd.lua",
            "runtime/bootstrap.lua"
    };

    private static boolean initialized;
    private static Context appContext;
    private static String luaRuntimeBootstrap;

    static {
        System.loadLibrary("engine");
    }

    private NativeEngine() {
    }

    public static synchronized void init(Context context) {
        if (initialized) {
            return;
        }
        appContext = context.getApplicationContext();
        AndroidHostBridge.init(appContext);
        luaRuntimeBootstrap = loadLuaRuntimeBootstrap(appContext);
        nativeInit();
        initialized = true;
    }

    public static String runLuaText(String code) {
        if (!RUNNING.compareAndSet(false, true)) {
            return "Engine is already running";
        }

        try {
            return nativeRunLuaText(luaRuntimeBootstrap + "\n" + code);
        } finally {
            RUNNING.set(false);
        }
    }

    public static void stop() {
        nativeStop();
    }

    public static boolean pause() {
        return nativePause();
    }

    public static boolean resume() {
        return nativeResume();
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

    public static String captureRootScreenJson() {
        return nativeCaptureRootScreenJson();
    }

    public static boolean releaseImage(int imageId) {
        return nativeReleaseImage(imageId);
    }

    /**
     * 从 assets/runtime 读取 Lua 运行时层。
     *
     * C++ 只暴露 native _host 表；m/lr/cd/useApi 这些命名空间和兼容逻辑都放在
     * Lua 层，后续补兼容函数时不需要改 native 代码。
     */
    private static String loadLuaRuntimeBootstrap(Context context) {
        StringBuilder builder = new StringBuilder();
        for (String assetPath : LUA_RUNTIME_ASSETS) {
            builder.append("\n-- ").append(assetPath).append("\n");
            builder.append(readAssetText(context, assetPath)).append('\n');
        }
        return builder.toString();
    }

    private static String readAssetText(Context context, String assetPath) {
        try (InputStream inputStream = context.getAssets().open(assetPath);
             ByteArrayOutputStream outputStream = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[4096];
            int readCount;
            while ((readCount = inputStream.read(buffer)) != -1) {
                outputStream.write(buffer, 0, readCount);
            }
            return outputStream.toString(StandardCharsets.UTF_8.name());
        } catch (IOException exception) {
            throw new IllegalStateException("read lua runtime asset failed: " + assetPath, exception);
        }
    }

    private static native void nativeInit();

    private static native String nativeRunLuaText(String code);

    private static native void nativeStop();

    private static native boolean nativePause();

    private static native boolean nativeResume();

    private static native String nativeDrainLogs(int afterId);

    private static native String nativeStatusJson(int taskId);

    private static native String nativeEngineVersion();

    private static native String nativeLuaVersion();

    private static native String nativeCaptureScreenJson();

    private static native String nativeCaptureRootScreenJson();

    private static native boolean nativeReleaseImage(int imageId);
}
