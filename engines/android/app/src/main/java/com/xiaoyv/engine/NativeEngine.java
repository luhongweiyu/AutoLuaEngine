/**
 * 文件用途：加载 libengine.so，并向 Java 层暴露 native 引擎命令调用入口。
 */
package com.xiaoyv.engine;

import android.content.Context;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

/**
 * Java 层统一 native 入口。
 *
 * 后续所有 Java/Kotlin 到 C++ 引擎的调用都从这里走，避免 JNI 方法散落在各处。
 */
public final class NativeEngine {
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

    /**
     * 调用 libengine.so 的统一 JSON 命令入口。
     *
     * App、悬浮窗、HTTP JSON-RPC 和后续 IDE 插件都应该优先走这里。Java 层只负责
     * 把 Android 必须保留在框架侧的对象传给 native，具体命令由 C++ 引擎统一处理。
     */
    public static String callJson(String method, String paramsJson) {
        return nativeCallJson(
                method == null ? "" : method,
                paramsJson == null || paramsJson.trim().isEmpty() ? "{}" : paramsJson,
                luaRuntimeBootstrap == null ? "" : luaRuntimeBootstrap
        );
    }

    /**
     * 复制当前截图为桌面工具使用的 XYVF 二进制帧。
     *
     * 帧头为 ASCII "XYVF"、little-endian int32 宽度和高度，后面紧跟 RGBA8888 点阵。
     * Java 和 HTTP 层只搬运字节，不重新截图、不编码图片也不写磁盘。
     */
    public static byte[] getScreenFrame() {
        return nativeGetScreenFrame();
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
            throw new IllegalStateException("读取 Lua 运行时资源失败：" + assetPath, exception);
        }
    }

    private static native void nativeInit();

    private static native String nativeCallJson(
            String method,
            String paramsJson,
            String luaRuntimeBootstrap
    );

    private static native byte[] nativeGetScreenFrame();
}
