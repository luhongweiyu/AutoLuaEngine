/**
 * 文件用途：管理 /sdcard/xiaoyv/extensions 中等待导入的扩展文件和扩展目录；导入只复制，
 * 不执行其中的 native 代码。
 */
package com.xiaoyv.engine;

import android.content.Context;
import android.os.Environment;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;

/**
 * 本地扩展导入目录。
 *
 * 共享目录只识别最外层的普通文件和目录。目录作为一个扩展包导入，内部相对路径会原样保留；
 * App 不根据名称、后缀、ABI、签名或内容推断其用途。导入副本位于私有目录，native 文件仅会在
 * 脚本实际请求功能时按需加载。
 */
public final class ExtensionCatalog {
    public static final String EXTENSIONS_DIRECTORY_NAME = "extensions";

    private static final String IMPORTED_DIRECTORY_NAME = "imported-extensions";
    private static final int COPY_BUFFER_BYTES = 64 * 1024;

    private ExtensionCatalog() {
    }

    /** 返回用户可直接复制扩展文件或扩展目录的固定目录。 */
    public static File getExtensionDirectory() {
        return new File(
                new File(Environment.getExternalStorageDirectory(), ScriptCatalog.STORAGE_ROOT_DIRECTORY_NAME),
                EXTENSIONS_DIRECTORY_NAME
        );
    }

    /** 返回展示给用户的稳定目录写法。 */
    public static String getExtensionDirectoryDisplayPath() {
        return "/sdcard/" + ScriptCatalog.STORAGE_ROOT_DIRECTORY_NAME
                + "/" + EXTENSIONS_DIRECTORY_NAME;
    }

    /** 创建共享扩展目录；沿用已有的所有文件访问授权。 */
    public static boolean ensureExtensionDirectory(Context context) {
        if (!ScriptCatalog.isScriptStorageAccessible(context)) {
            return false;
        }
        File directory = getExtensionDirectory();
        return (directory.isDirectory() || directory.mkdirs()) && directory.isDirectory();
    }

    /** 列出共享目录最外层的普通文件和目录，不递归扫描目录内部。 */
    public static ExtensionItem[] listExtensions(Context context) {
        if (!ensureExtensionDirectory(context)) {
            return new ExtensionItem[0];
        }
        File[] entries = getExtensionDirectory().listFiles(file -> file.isFile() || file.isDirectory());
        if (entries == null || entries.length == 0) {
            return new ExtensionItem[0];
        }
        Arrays.sort(entries, (left, right) -> {
            if (left.isDirectory() != right.isDirectory()) {
                return left.isDirectory() ? -1 : 1;
            }
            return left.getName().toLowerCase(Locale.US)
                    .compareTo(right.getName().toLowerCase(Locale.US));
        });

        List<ExtensionItem> items = new ArrayList<>(entries.length);
        for (File entry : entries) {
            boolean directory = entry.isDirectory();
            items.add(new ExtensionItem(
                    entry.getName(),
                    entry.getAbsolutePath(),
                    directory,
                    directory ? 0L : entry.length(),
                    entry.lastModified(),
                    hasImportedEntry(context, entry.getName(), directory)
            ));
        }
        return items.toArray(new ExtensionItem[0]);
    }

    /**
     * 导入一个最外层文件或目录。目录整体替换旧副本，避免用户更新扩展包后留下旧依赖或旧模型。
     */
    public static ImportResult importExtension(Context context, ExtensionItem item) {
        if (context == null || item == null) {
            return ImportResult.failure("扩展条目不存在");
        }
        if (!ensureExtensionDirectory(context)) {
            return ImportResult.failure("无法访问扩展目录：" + getExtensionDirectoryDisplayPath());
        }

        File source;
        File root;
        try {
            source = new File(item.filePath).getCanonicalFile();
            root = getExtensionDirectory().getCanonicalFile();
        } catch (IOException error) {
            return ImportResult.failure("读取扩展路径失败：" + safeMessage(error));
        }
        if (!isTopLevelEntry(source, root)) {
            return ImportResult.failure("扩展条目已不存在或不在扩展目录最外层");
        }

        File importedDirectory = getImportedDirectory(context);
        if (!importedDirectory.isDirectory() && !importedDirectory.mkdirs()) {
            return ImportResult.failure("无法创建导入目录");
        }
        File destination = new File(importedDirectory, source.getName());
        File temporary = new File(
                importedDirectory,
                "." + source.getName() + "." + System.nanoTime() + ".importing"
        );
        String copyError = copyEntryAsReadOnly(source, temporary, source);
        if (!copyError.isEmpty()) {
            deleteRecursively(temporary);
            return ImportResult.failure(copyError);
        }
        if (destination.exists() && !deleteRecursively(destination)) {
            deleteRecursively(temporary);
            return ImportResult.failure("无法替换已导入的扩展");
        }
        if (!temporary.renameTo(destination)) {
            deleteRecursively(temporary);
            return ImportResult.failure("无法完成扩展导入");
        }
        return ImportResult.success((source.isDirectory() ? "已导入文件夹：" : "已导入文件：")
                + source.getName());
    }

    /**
     * 返回已导入的相对路径文件。相对路径以导入目录为根，例如 rapidocr/libonnxruntime.so。
     */
    static File getImportedExtension(Context context, String relativePath) {
        File file = getImportedPath(context, relativePath);
        return file != null && file.isFile() && file.canRead() ? file : null;
    }

    private static boolean hasImportedEntry(Context context, String relativePath, boolean directory) {
        File entry = getImportedPath(context, relativePath);
        return entry != null && entry.canRead() && (directory ? entry.isDirectory() : entry.isFile());
    }

    private static File getImportedPath(Context context, String relativePath) {
        if (context == null || !isSafeRelativePath(relativePath)) {
            return null;
        }
        try {
            File root = getImportedDirectory(context).getCanonicalFile();
            File candidate = new File(root, relativePath).getCanonicalFile();
            return isWithin(root, candidate) ? candidate : null;
        } catch (IOException error) {
            return null;
        }
    }

    private static File getImportedDirectory(Context context) {
        return context.getDir(IMPORTED_DIRECTORY_NAME, Context.MODE_PRIVATE);
    }

    private static boolean isTopLevelEntry(File source, File root) {
        return (source.isFile() || source.isDirectory())
                && source.getParentFile() != null
                && source.getParentFile().equals(root);
    }

    private static boolean isSafeRelativePath(String value) {
        if (value == null || value.isEmpty() || value.startsWith("/") || value.indexOf('\\') >= 0) {
            return false;
        }
        String[] segments = value.split("/", -1);
        for (String segment : segments) {
            if (segment.isEmpty() || ".".equals(segment) || "..".equals(segment)) {
                return false;
            }
        }
        return true;
    }

    private static String copyEntryAsReadOnly(File source, File destination, File sourceRoot) {
        try {
            File canonicalSource = source.getCanonicalFile();
            if (!isWithin(sourceRoot, canonicalSource)) {
                return "扩展目录包含超出自身范围的路径";
            }
            if (canonicalSource.isDirectory()) {
                return copyDirectoryAsReadOnly(canonicalSource, destination, sourceRoot);
            }
            if (canonicalSource.isFile()) {
                return copyFileAsReadOnly(canonicalSource, destination);
            }
            return "扩展条目不是可读取的文件或目录";
        } catch (IOException error) {
            return "读取扩展条目失败：" + safeMessage(error);
        }
    }

    private static String copyDirectoryAsReadOnly(File source, File destination, File sourceRoot) {
        if (!destination.mkdirs() && !destination.isDirectory()) {
            return "无法创建扩展目录副本";
        }
        File[] children = source.listFiles();
        if (children == null) {
            return "无法读取扩展目录";
        }
        for (File child : children) {
            String error = copyEntryAsReadOnly(child, new File(destination, child.getName()), sourceRoot);
            if (!error.isEmpty()) {
                return error;
            }
        }
        if (!destination.setReadOnly()) {
            return "无法将已导入的扩展目录设为只读";
        }
        return "";
    }

    private static String copyFileAsReadOnly(File source, File destination) {
        File parent = destination.getParentFile();
        if (parent == null || (!parent.isDirectory() && !parent.mkdirs())) {
            return "无法创建扩展文件目录";
        }
        try (FileInputStream input = new FileInputStream(source);
             FileOutputStream output = new FileOutputStream(destination, false)) {
            byte[] buffer = new byte[COPY_BUFFER_BYTES];
            int count;
            while ((count = input.read(buffer)) != -1) {
                output.write(buffer, 0, count);
            }
            output.getFD().sync();
        } catch (IOException error) {
            return "导入扩展文件失败：" + safeMessage(error);
        }
        return destination.setReadOnly() ? "" : "无法将已导入的扩展文件设为只读";
    }

    private static boolean deleteRecursively(File file) {
        if (file == null || !file.exists()) {
            return true;
        }
        file.setWritable(true);
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children == null) {
                return false;
            }
            for (File child : children) {
                if (!deleteRecursively(child)) {
                    return false;
                }
            }
        }
        return file.delete();
    }

    private static boolean isWithin(File root, File candidate) {
        String rootPath = root.getPath();
        String candidatePath = candidate.getPath();
        return candidatePath.equals(rootPath)
                || candidatePath.startsWith(rootPath + File.separator);
    }

    private static String safeMessage(Throwable throwable) {
        String message = throwable == null ? "" : throwable.getMessage();
        return message == null || message.trim().isEmpty()
                ? throwable.getClass().getSimpleName()
                : message;
    }

    public static final class ExtensionItem {
        public final String fileName;
        public final String filePath;
        public final boolean directory;
        public final long sizeBytes;
        public final long modifiedAt;
        public final boolean imported;

        private ExtensionItem(
                String fileName,
                String filePath,
                boolean directory,
                long sizeBytes,
                long modifiedAt,
                boolean imported
        ) {
            this.fileName = fileName;
            this.filePath = filePath;
            this.directory = directory;
            this.sizeBytes = sizeBytes;
            this.modifiedAt = modifiedAt;
            this.imported = imported;
        }
    }

    public static final class ImportResult {
        public final boolean success;
        public final String message;

        private ImportResult(boolean success, String message) {
            this.success = success;
            this.message = message == null ? "" : message;
        }

        private static ImportResult success(String message) {
            return new ImportResult(true, message);
        }

        private static ImportResult failure(String message) {
            return new ImportResult(false, message);
        }
    }
}
