/**
 * 文件用途：管理共享脚本目录，负责复制示例、扫描文件和记录当前选择。
 */
package com.autolua.engine;

import android.Manifest;
import android.content.Context;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Environment;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CharsetDecoder;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;

/**
 * App 侧脚本目录。
 *
 * 用户脚本统一保存在 /sdcard/AutoLuaEngine/scripts。App 获得所有文件访问权限后，直接把
 * 内置示例复制到这个目录，并从这个目录读取文件列表。不存在私有目录、中转副本或迁移流程。
 */
public final class ScriptCatalog {
    public static final String PREF_NAME = "script_state";
    public static final String KEY_SELECTED_SCRIPT_PATH = "selected_script_path";
    public static final String STORAGE_ROOT_DIRECTORY_NAME = "AutoLuaEngine";
    public static final String SCRIPTS_DIR_NAME = "scripts";
    public static final String DEFAULT_SCRIPT_FILE_NAME = "main.lua";
    public static final String PACKAGE_EXTENSION = ".alpkg";

    private static final String ASSET_SCRIPT_DIR = "scripts";
    private static final int DESCRIPTION_READ_BYTES = 2048;
    private static final int MAX_DESCRIPTION_LENGTH = 42;
    private static final String[] SAMPLE_SCRIPT_NAMES = {
            "main.lua",
            "error.lua",
            "loop.lua",
            "input.lua",
            "java_import.lua",
            "screen.lua",
            "screen_benchmark.lua",
            "ui.lua",
            "ui_demo.html"
    };

    private ScriptCatalog() {
    }

    /**
     * 返回唯一共享脚本目录。真实文件路径后续可直接供 Lua、插件和外部工具复用。
     */
    public static File getScriptDirectory() {
        return new File(
                new File(Environment.getExternalStorageDirectory(), STORAGE_ROOT_DIRECTORY_NAME),
                SCRIPTS_DIR_NAME
        );
    }

    /**
     * 返回用户熟悉的脚本目录显示路径。
     */
    public static String getScriptDirectoryDisplayPath() {
        return "/sdcard/" + STORAGE_ROOT_DIRECTORY_NAME + "/" + SCRIPTS_DIR_NAME;
    }

    /**
     * 判断当前进程是否可直接访问共享脚本目录。
     *
     * Android 11 及以上使用系统所有文件访问授权；Android 10 及以下同时申请传统读写存储权限。
     */
    public static boolean isScriptStorageAccessible(Context context) {
        if (context == null
                || !Environment.MEDIA_MOUNTED.equals(Environment.getExternalStorageState())) {
            return false;
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            return Environment.isExternalStorageManager();
        }

        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return true;
        }
        return context.checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE)
                == PackageManager.PERMISSION_GRANTED
                && context.checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE)
                == PackageManager.PERMISSION_GRANTED;
    }

    /**
     * 创建共享脚本目录，并在不覆盖现有文件的前提下复制内置示例。
     *
     * @return 目录可直接读写时返回 true。
     */
    public static boolean ensureScriptDirectory(Context context) {
        if (!isScriptStorageAccessible(context)) {
            return false;
        }

        File scriptDirectory = getScriptDirectory();
        if (!scriptDirectory.exists() && !scriptDirectory.mkdirs()) {
            return false;
        }
        if (!scriptDirectory.isDirectory()) {
            return false;
        }

        for (String fileName : SAMPLE_SCRIPT_NAMES) {
            File targetFile = new File(scriptDirectory, fileName);
            if (!targetFile.exists()) {
                copyAssetScript(context, fileName, targetFile);
            }
        }
        return true;
    }

    public static ScriptItem[] listScripts(Context context) {
        if (!ensureScriptDirectory(context)) {
            return new ScriptItem[0];
        }

        File[] files = getScriptDirectory().listFiles(File::isFile);
        if (files == null || files.length == 0) {
            return new ScriptItem[0];
        }

        Arrays.sort(files, (left, right) -> left.getName()
                .toLowerCase(Locale.US)
                .compareTo(right.getName().toLowerCase(Locale.US)));

        List<ScriptItem> items = new ArrayList<>();
        for (File file : files) {
            items.add(toScriptItem(file));
        }
        return items.toArray(new ScriptItem[0]);
    }

    public static ScriptItem getSelectedScript(Context context) {
        ScriptItem[] scripts = listScripts(context);
        if (scripts.length == 0) {
            return null;
        }

        String savedPath = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
                .getString(KEY_SELECTED_SCRIPT_PATH, "");
        ScriptItem selected = findByPath(context, savedPath);
        if (selected != null) {
            return selected;
        }

        ScriptItem fallback = findByFileName(scripts, DEFAULT_SCRIPT_FILE_NAME);
        if (fallback == null) {
            fallback = scripts[0];
        }
        setSelectedScript(context, fallback);
        return fallback;
    }

    public static void setSelectedScript(Context context, ScriptItem item) {
        if (context == null || item == null) {
            return;
        }

        context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
                .edit()
                .putString(KEY_SELECTED_SCRIPT_PATH, item.filePath)
                .apply();
    }

    public static ScriptItem findByPath(Context context, String path) {
        if (path == null || path.trim().isEmpty()) {
            return null;
        }

        String normalizedPath = normalizeSavedPath(path);
        ScriptItem[] scripts = listScripts(context);
        for (ScriptItem item : scripts) {
            if (item.filePath.equals(normalizedPath)) {
                return item;
            }
        }
        return null;
    }

    public static String readScriptText(String filePath) throws IOException {
        try (FileInputStream inputStream = new FileInputStream(filePath)) {
            return readAllText(inputStream);
        }
    }

    private static ScriptItem toScriptItem(File file) {
        String fileName = file.getName();
        String language = detectLanguage(fileName);
        String description = readDescriptionPreview(file);
        String displayPath = STORAGE_ROOT_DIRECTORY_NAME
                + "/"
                + SCRIPTS_DIR_NAME
                + "/"
                + fileName;
        return new ScriptItem(
                fileName,
                file.getAbsolutePath(),
                displayPath,
                description,
                language,
                isRunnableLanguage(language),
                file.length(),
                file.lastModified()
        );
    }

    private static ScriptItem findByFileName(ScriptItem[] scripts, String fileName) {
        for (ScriptItem item : scripts) {
            if (item.fileName.equals(fileName)) {
                return item;
            }
        }
        return null;
    }

    private static String normalizeSavedPath(String path) {
        String trimmed = path.trim();
        File file = new File(trimmed);
        if (file.isAbsolute()) {
            return file.getAbsolutePath();
        }

        String fileName = trimmed;
        int separatorIndex = Math.max(trimmed.lastIndexOf('/'), trimmed.lastIndexOf('\\'));
        if (separatorIndex >= 0 && separatorIndex + 1 < trimmed.length()) {
            fileName = trimmed.substring(separatorIndex + 1);
        }
        return new File(getScriptDirectory(), fileName).getAbsolutePath();
    }

    /**
     * 把一个内置示例直接写入共享目录。写入失败会删除半成品，避免列表显示损坏文件。
     */
    private static void copyAssetScript(Context context, String fileName, File targetFile) {
        String assetPath = ASSET_SCRIPT_DIR + "/" + fileName;
        try (InputStream inputStream = context.getAssets().open(assetPath);
             FileOutputStream outputStream = new FileOutputStream(targetFile)) {
            copyStream(inputStream, outputStream);
        } catch (IOException ignored) {
            // 内置示例写入失败不影响用户已存在脚本的扫描。
            targetFile.delete();
        }
    }

    private static void copyStream(InputStream inputStream, FileOutputStream outputStream)
            throws IOException {
        byte[] buffer = new byte[4096];
        int readCount;
        while ((readCount = inputStream.read(buffer)) != -1) {
            outputStream.write(buffer, 0, readCount);
        }
        outputStream.flush();
    }

    private static String readAllText(InputStream inputStream) throws IOException {
        try (ByteArrayOutputStream outputStream = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[4096];
            int readCount;
            while ((readCount = inputStream.read(buffer)) != -1) {
                outputStream.write(buffer, 0, readCount);
            }
            return outputStream.toString(StandardCharsets.UTF_8.name());
        }
    }

    private static String readDescriptionPreview(File file) {
        byte[] bytes = readFilePrefix(file, DESCRIPTION_READ_BYTES);
        if (bytes.length == 0) {
            return "";
        }

        int offset = hasUtf8Bom(bytes) ? 3 : 0;
        int length = trimIncompleteUtf8Tail(bytes, offset, bytes.length - offset);
        if (length <= 0) {
            return "";
        }

        CharsetDecoder decoder = StandardCharsets.UTF_8.newDecoder()
                .onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT);
        try {
            String text = decoder.decode(ByteBuffer.wrap(bytes, offset, length)).toString();
            return normalizeDescriptionPreview(text);
        } catch (CharacterCodingException ignored) {
            // 文件开头不是合法 UTF-8 时，列表备注直接留空，避免把乱码显示到界面上。
            return "";
        }
    }

    private static byte[] readFilePrefix(File file, int maxBytes) {
        if (file.length() <= 0) {
            return new byte[0];
        }

        int limit = (int) Math.min(Math.max(0, file.length()), maxBytes);
        byte[] buffer = new byte[limit];
        try (FileInputStream inputStream = new FileInputStream(file)) {
            int total = 0;
            while (total < limit) {
                int readCount = inputStream.read(buffer, total, limit - total);
                if (readCount == -1) {
                    break;
                }
                total += readCount;
            }
            return total == limit ? buffer : Arrays.copyOf(buffer, total);
        } catch (IOException ignored) {
            return new byte[0];
        }
    }

    private static boolean hasUtf8Bom(byte[] bytes) {
        return bytes.length >= 3
                && (bytes[0] & 0xFF) == 0xEF
                && (bytes[1] & 0xFF) == 0xBB
                && (bytes[2] & 0xFF) == 0xBF;
    }

    private static int trimIncompleteUtf8Tail(byte[] bytes, int offset, int length) {
        int end = offset + length;
        int leadIndex = end - 1;
        while (leadIndex >= offset && (bytes[leadIndex] & 0xC0) == 0x80) {
            leadIndex--;
        }
        if (leadIndex < offset) {
            return 0;
        }

        int leadByte = bytes[leadIndex] & 0xFF;
        int expectedBytes;
        if ((leadByte & 0x80) == 0) {
            return length;
        } else if ((leadByte & 0xE0) == 0xC0) {
            expectedBytes = 2;
        } else if ((leadByte & 0xF0) == 0xE0) {
            expectedBytes = 3;
        } else if ((leadByte & 0xF8) == 0xF0) {
            expectedBytes = 4;
        } else {
            return length;
        }

        int actualBytes = end - leadIndex;
        return actualBytes < expectedBytes ? leadIndex - offset : length;
    }

    private static String normalizeDescriptionPreview(String text) {
        if (text == null || text.indexOf('\uFFFD') >= 0) {
            return "";
        }

        StringBuilder builder = new StringBuilder(MAX_DESCRIPTION_LENGTH + 3);
        boolean pendingSpace = false;
        String trimmedText = text.trim();
        for (int index = 0; index < trimmedText.length(); index++) {
            char value = trimmedText.charAt(index);
            if (Character.isWhitespace(value) || Character.isISOControl(value)) {
                pendingSpace = builder.length() > 0;
                continue;
            }

            if (pendingSpace) {
                builder.append(' ');
                pendingSpace = false;
            }
            builder.append(value);
            if (builder.length() >= MAX_DESCRIPTION_LENGTH) {
                return builder.toString() + "...";
            }
        }
        return builder.toString();
    }

    private static String detectLanguage(String fileName) {
        String lowerName = fileName.toLowerCase(Locale.US);
        if (lowerName.endsWith(".lua")) {
            return "lua";
        }
        if (lowerName.endsWith(PACKAGE_EXTENSION)) {
            return "alpkg";
        }
        if (lowerName.endsWith(".js")) {
            return "js";
        }
        if (lowerName.endsWith(".go")) {
            return "go";
        }
        return "text";
    }

    private static boolean isRunnableLanguage(String language) {
        return "lua".equals(language) || "alpkg".equals(language);
    }

    public static final class ScriptItem {
        public final String fileName;
        public final String filePath;
        public final String displayPath;
        public final String description;
        public final String language;
        public final boolean runnable;
        public final long sizeBytes;
        public final long modifiedAt;

        private ScriptItem(
                String fileName,
                String filePath,
                String displayPath,
                String description,
                String language,
                boolean runnable,
                long sizeBytes,
                long modifiedAt) {
            this.fileName = fileName;
            this.filePath = filePath;
            this.displayPath = displayPath;
            this.description = description;
            this.language = language;
            this.runnable = runnable;
            this.sizeBytes = sizeBytes;
            this.modifiedAt = modifiedAt;
        }
    }
}
