/**
 * 文件用途：在脚本运行需要时加载已经由扩展页导入的 native 文件。
 */
package com.xiaoyv.engine;

import android.content.Context;

import java.io.File;

/**
 * 已导入 native 扩展的按需加载器。
 *
 * 它不扫描共享目录、不判断 ABI、不校验文件名、签名、哈希或版本。文件是否适合当前设备和
 * 当前功能完全由用户负责；Android linker 的加载结果就是唯一判断。扩展页的“导入”已经把
 * 用户选中的文件保留为私有只读副本，但不会提前执行这里的加载逻辑。
 */
final class OptionalNativeRuntimeLoader {
    private OptionalNativeRuntimeLoader() {
    }

    /**
     * 在脚本实际请求某个功能时，以原始导入文件名按给定顺序加载。
     *
     * 多文件运行时的调用方负责给出正确的依赖顺序。例如 OpenCV 先加载 C++ 运行库、再加载
     * OpenCV 本体。这里不推断文件类型、ABI、依赖或内容，也不尝试补救错误的文件组合。
     */
    static LoadResult loadImported(Context context, String... fileNames) {
        if (context == null) {
            return LoadResult.failure("Android 应用上下文尚未初始化");
        }
        if (fileNames == null || fileNames.length == 0) {
            return LoadResult.failure("没有指定要加载的扩展文件");
        }

        File lastLoaded = null;
        for (String fileName : fileNames) {
            File library = ExtensionCatalog.getImportedExtension(context, fileName);
            if (library == null) {
                return LoadResult.failure(
                        "未导入扩展文件 " + safe(fileName)
                                + "；请先在扩展页从 "
                                + ExtensionCatalog.getExtensionDirectoryDisplayPath()
                                + " 导入该文件"
                );
            }
            try {
                System.load(library.getAbsolutePath());
                lastLoaded = library;
            } catch (UnsatisfiedLinkError | SecurityException error) {
                return LoadResult.failure(
                        "加载扩展文件 " + safe(fileName) + " 失败：" + safeMessage(error)
                );
            }
        }
        return LoadResult.success(lastLoaded);
    }

    private static String safe(String value) {
        return value == null ? "" : value;
    }

    private static String safeMessage(Throwable throwable) {
        String message = throwable == null ? "" : throwable.getMessage();
        return message == null || message.trim().isEmpty()
                ? throwable.getClass().getSimpleName()
                : message;
    }

    static final class LoadResult {
        final boolean loaded;
        final String loadedLibraryPath;
        final String error;

        private LoadResult(boolean loaded, String loadedLibraryPath, String error) {
            this.loaded = loaded;
            this.loadedLibraryPath = loadedLibraryPath == null ? "" : loadedLibraryPath;
            this.error = error == null ? "" : error;
        }

        static LoadResult success(File library) {
            return new LoadResult(true, library == null ? "" : library.getPath(), "");
        }

        static LoadResult failure(String error) {
            return new LoadResult(false, "", error);
        }
    }
}
