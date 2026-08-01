/**
 * 文件用途：管理 /sdcard/xiaoyv/extensions 中等待导入的扩展文件；导入只复制，不执行 native 代码。
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
 * 本地扩展文件目录。
 *
 * 共享目录刻意保持平铺：`/sdcard/xiaoyv/extensions/<文件名>`。不根据文件名、后缀、ABI、
 * 签名或内容推断功能类别；用户放入什么文件、以什么名字导入，由用户自己负责。
 *
 * “导入”只把用户选中的文件以原文件名复制为 App 私有目录中的只读副本，绝不在点击导入时
 * 调用 {@code System.load}。真正加载由脚本运行期间请求该文件的功能完成。
 */
public final class ExtensionCatalog {
    public static final String EXTENSIONS_DIRECTORY_NAME = "extensions";

    private static final String IMPORTED_DIRECTORY_NAME = "imported-extensions";
    private static final int COPY_BUFFER_BYTES = 64 * 1024;

    private ExtensionCatalog() {
    }

    /** 返回用户可直接复制文件的固定目录。 */
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

    /** 列出共享扩展目录最外层的全部普通文件，不对文件类型作任何过滤。 */
    public static ExtensionItem[] listExtensions(Context context) {
        if (!ensureExtensionDirectory(context)) {
            return new ExtensionItem[0];
        }
        File[] files = getExtensionDirectory().listFiles(File::isFile);
        if (files == null || files.length == 0) {
            return new ExtensionItem[0];
        }
        Arrays.sort(files, (left, right) -> left.getName()
                .toLowerCase(Locale.US)
                .compareTo(right.getName().toLowerCase(Locale.US)));

        List<ExtensionItem> items = new ArrayList<>(files.length);
        for (File file : files) {
            items.add(new ExtensionItem(
                    file.getName(),
                    file.getAbsolutePath(),
                    file.length(),
                    file.lastModified(),
                    getImportedExtension(context, file.getName()) != null
            ));
        }
        return items.toArray(new ExtensionItem[0]);
    }

    /**
     * 导入一个列表中的文件。只执行文件复制和只读标记，不校验内容，也不尝试加载。
     */
    public static ImportResult importExtension(Context context, ExtensionItem item) {
        if (context == null || item == null) {
            return ImportResult.failure("扩展文件不存在");
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
            return ImportResult.failure("读取扩展文件路径失败：" + safeMessage(error));
        }
        // 只接受当前平铺列表中的真实文件。这个检查仅限制目录边界，不解释或限制文件名与内容。
        if (!source.isFile() || source.getParentFile() == null || !source.getParentFile().equals(root)) {
            return ImportResult.failure("扩展文件已不存在或不在扩展目录中");
        }

        File importedDirectory = getImportedDirectory(context);
        if (!importedDirectory.isDirectory() && !importedDirectory.mkdirs()) {
            return ImportResult.failure("无法创建导入目录");
        }
        File destination = new File(importedDirectory, source.getName());
        String copyError = copyAsReadOnly(source, destination);
        if (!copyError.isEmpty()) {
            return ImportResult.failure(copyError);
        }
        return ImportResult.success("已导入：" + source.getName());
    }

    /**
     * 返回已导入的同名文件。供运行时按需加载使用；这里不会调用 System.load。
     */
    static File getImportedExtension(Context context, String fileName) {
        if (context == null || !isPlainFileName(fileName)) {
            return null;
        }
        File file = new File(getImportedDirectory(context), fileName);
        return file.isFile() && file.canRead() ? file : null;
    }

    private static File getImportedDirectory(Context context) {
        return context.getDir(IMPORTED_DIRECTORY_NAME, Context.MODE_PRIVATE);
    }

    private static String copyAsReadOnly(File source, File destination) {
        File parent = destination.getParentFile();
        if (parent == null || (!parent.isDirectory() && !parent.mkdirs())) {
            return "无法创建扩展导入目录";
        }
        File temporary = new File(parent, destination.getName() + ".importing");
        if (temporary.exists() && !temporary.delete()) {
            return "无法清理扩展临时文件";
        }

        try (FileInputStream input = new FileInputStream(source);
             FileOutputStream output = new FileOutputStream(temporary, false)) {
            byte[] buffer = new byte[COPY_BUFFER_BYTES];
            int count;
            while ((count = input.read(buffer)) != -1) {
                output.write(buffer, 0, count);
            }
            output.getFD().sync();
        } catch (IOException error) {
            deleteQuietly(temporary);
            return "导入扩展文件失败：" + safeMessage(error);
        }

        if (!temporary.setReadOnly()) {
            deleteQuietly(temporary);
            return "无法将已导入扩展设为只读";
        }
        if (destination.exists() && !destination.delete()) {
            deleteQuietly(temporary);
            return "无法替换已导入扩展";
        }
        if (!temporary.renameTo(destination)) {
            deleteQuietly(temporary);
            return "无法完成扩展导入";
        }
        return "";
    }

    private static boolean isPlainFileName(String value) {
        return value != null
                && !value.isEmpty()
                && !".".equals(value)
                && !"..".equals(value)
                && value.indexOf('/') < 0
                && value.indexOf('\\') < 0;
    }

    private static void deleteQuietly(File file) {
        if (file != null && file.exists()) {
            file.delete();
        }
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
        public final long sizeBytes;
        public final long modifiedAt;
        public final boolean imported;

        private ExtensionItem(
                String fileName,
                String filePath,
                long sizeBytes,
                long modifiedAt,
                boolean imported
        ) {
            this.fileName = fileName;
            this.filePath = filePath;
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
