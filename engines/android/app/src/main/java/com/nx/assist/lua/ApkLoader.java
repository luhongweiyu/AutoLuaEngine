/**
 * 文件用途：兼容懒人精灵 LuaEngine.loadApk 返回对象，加载外部 APK/JAR/DEX 类。
 */
package com.nx.assist.lua;

import android.content.Context;

import com.xiaoyv.engine.AndroidHostBridge;

import java.io.File;

import dalvik.system.DexClassLoader;

/**
 * Android 插件类加载器。
 *
 * LuaEngine.loadApk 负责把资源名称或绝对路径解析成文件，本类只持有对应
 * DexClassLoader，并通过 loadClass 返回 Java Class 对象给 Lua-Java 桥继续调用。
 */
public final class ApkLoader {
    private final String apkPath;
    private final DexClassLoader classLoader;

    ApkLoader(String apkPath) {
        Context context = AndroidHostBridge.applicationContext();
        if (context == null) {
            throw new IllegalStateException("Android Context 尚未初始化");
        }

        File optimizedDirectory = new File(context.getCodeCacheDir(), "lua_apk_plugins");
        if (!optimizedDirectory.exists() && !optimizedDirectory.mkdirs()) {
            throw new IllegalStateException("无法创建插件优化目录：" + optimizedDirectory);
        }

        this.apkPath = apkPath;
        this.classLoader = new DexClassLoader(
                apkPath,
                optimizedDirectory.getAbsolutePath(),
                new File(apkPath).getParent(),
                context.getClassLoader()
        );
    }

    /**
     * 从当前 APK/JAR/DEX 插件中加载完整类名。
     *
     * 失败时返回 null，与懒人精灵文档中的 `if plugin ~= nil` 使用方式一致。
     */
    public Class<?> loadClass(String className) {
        try {
            return Class.forName(className, true, classLoader);
        } catch (ClassNotFoundException exception) {
            return null;
        }
    }

    /**
     * 返回当前插件的实际文件路径，便于日志和脚本排查加载来源。
     */
    public String getApkPath() {
        return apkPath;
    }
}
