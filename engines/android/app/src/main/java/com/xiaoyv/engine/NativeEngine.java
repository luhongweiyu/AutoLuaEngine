/**
 * 文件用途：加载 libengine.so，并向 Java 层暴露 native 引擎命令调用入口。
 */
package com.xiaoyv.engine;

import android.content.Context;
import android.content.res.AssetManager;
import android.graphics.Bitmap;
import android.util.DisplayMetrics;
import android.view.Surface;

/**
 * Java 层统一 native 入口。
 *
 * 后续所有 Java/Kotlin 到 C++ 引擎的调用都从这里走，避免 JNI 方法散落在各处。
 */
public final class NativeEngine {
    private static boolean libraryLoaded;
    private static boolean initialized;
    private static Context appContext;

    private NativeEngine() {
    }

    /**
     * Local Worker 使用系统 nativeLibraryDir 搜索；Root app_process 必须传入安装目录并按
     * 绝对路径加载。两种启动方式最终仍进入同一个 libengine.so。
     */
    public static synchronized void loadLibrary(String nativeLibraryDirectory) {
        if (libraryLoaded) {
            return;
        }
        if (nativeLibraryDirectory == null || nativeLibraryDirectory.trim().isEmpty()) {
            System.loadLibrary("engine");
        } else {
            System.load(new java.io.File(nativeLibraryDirectory, "libengine.so").getAbsolutePath());
        }
        libraryLoaded = true;
    }

    /** 仅供独立 Root app_process 用 bootstrap ClassLoader 初始化系统 Java native。 */
    static synchronized void loadRootSystemLibrary(String absolutePath) {
        if (!libraryLoaded) {
            throw new IllegalStateException("native 引擎尚未加载");
        }
        if (absolutePath == null || absolutePath.trim().isEmpty()) {
            throw new IllegalArgumentException("系统 native 库路径为空");
        }
        nativeLoadRootSystemLibrary(absolutePath);
    }

    public static synchronized void init(Context context) {
        if (initialized) {
            return;
        }
        loadLibrary(null);
        Context application = context.getApplicationContext();
        appContext = application == null ? context : application;
        AndroidHostBridge.init(appContext);
        nativeInit(appContext.getAssets());
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
                paramsJson == null || paramsJson.trim().isEmpty() ? "{}" : paramsJson
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
     * 把 Java Bitmap 复制为脚本任务的固定截图；传 null 恢复物理屏幕。
     *
     * 该入口只供兼容 LuaEngine.setSnapCacheBitmap 使用。native 会立即复制像素，
     * 不会持有 Java Bitmap，也不会接管其回收。
     */
    public static boolean setScreenBitmap(Bitmap bitmap) {
        if (bitmap == null) {
            return nativeSetScreenBitmap(null, 0, 0);
        }
        if (appContext == null) {
            return false;
        }
        DisplayMetrics metrics = appContext.getResources().getDisplayMetrics();
        return nativeSetScreenBitmap(bitmap, metrics.widthPixels, metrics.heightPixels);
    }

    /** 把 ScriptImGuiService 创建的 Surface 附着到 native EGL 渲染线程。 */
    public static boolean attachImGuiSurface(Surface surface) {
        return surface != null && surface.isValid() && nativeAttachImGuiSurface(surface);
    }

    /** Surface 销毁或脚本结束时同步停止 native ImGui 渲染线程。 */
    public static void detachImGuiSurface() {
        nativeDetachImGuiSurface();
    }

    /** 把 WindowManager 或 Surface 创建阶段的异步错误送回等待中的脚本。 */
    public static void notifyImGuiSurfaceFailure(String message) {
        nativeNotifyImGuiSurfaceFailure(message == null ? "ImGui Surface 创建失败" : message);
    }

    /** 转发 Android 触摸事件；native 只使用一个活动 pointerId。 */
    public static void enqueueImGuiTouch(int action, int pointerId, float x, float y) {
        nativeEnqueueImGuiTouch(action, pointerId, x, y);
    }

    /** 转发输入法最终确认的 UTF-8 文本。 */
    public static void enqueueImGuiText(String text) {
        nativeEnqueueImGuiText(text == null ? "" : text);
    }

    /** 转发实体键或输入法删除、回车等 KeyEvent。 */
    public static void enqueueImGuiKey(int action, int keyCode, int unicodeCodePoint, int metaState) {
        nativeEnqueueImGuiKey(action, keyCode, unicodeCodePoint, metaState);
    }

    /** 转发鼠标或触控板滚轮。 */
    public static void enqueueImGuiScroll(float horizontal, float vertical) {
        nativeEnqueueImGuiScroll(horizontal, vertical);
    }

    private static native void nativeLoadRootSystemLibrary(String absolutePath);

    /**
     * 初始化 native 引擎及固定 Lua 运行时模块。
     *
     * Java 只提供当前 APK 的 AssetManager；模块白名单、路径、读取和 package.preload 注册
     * 全部由 native Lua runtime 统一管理。
     */
    private static native void nativeInit(AssetManager assetManager);

    private static native String nativeCallJson(
            String method,
            String paramsJson
    );

    private static native byte[] nativeGetScreenFrame();

    private static native boolean nativeSetScreenBitmap(
            Bitmap bitmap,
            int screenWidth,
            int screenHeight
    );

    private static native boolean nativeAttachImGuiSurface(Surface surface);

    private static native void nativeDetachImGuiSurface();

    private static native void nativeNotifyImGuiSurfaceFailure(String message);

    private static native void nativeEnqueueImGuiTouch(
            int action,
            int pointerId,
            float x,
            float y
    );

    private static native void nativeEnqueueImGuiText(String text);

    private static native void nativeEnqueueImGuiKey(
            int action,
            int keyCode,
            int unicodeCodePoint,
            int metaState
    );

    private static native void nativeEnqueueImGuiScroll(float horizontal, float vertical);
}
