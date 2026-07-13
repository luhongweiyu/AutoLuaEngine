/**
 * 文件用途：管理共享脚本目录，负责复制示例、扫描文件和记录当前选择。
 */
package com.autolua.engine;

import android.Manifest;
import android.content.Context;
import android.content.SharedPreferences;
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
 * 默认目录是 /sdcard/AutoLuaEngine/scripts，用户也可以在设置页输入共享存储中的其他目录。
 * App 和引擎始终读取同一真实路径，不存在私有目录、中转副本或迁移流程。
 */
public final class ScriptCatalog {
    public static final String PREF_NAME = "script_state";
    public static final String KEY_SELECTED_SCRIPT_PATH = "selected_script_path";
    public static final String STORAGE_ROOT_DIRECTORY_NAME = "AutoLuaEngine";
    public static final String SCRIPTS_DIR_NAME = "scripts";
    public static final String DEFAULT_SCRIPT_FILE_NAME = "main.lua";
    public static final String PACKAGE_EXTENSION = ".alpkg";

    private static final String KEY_SCRIPT_DIRECTORY_PATH = "script_directory_path";
    private static final String KEY_SAMPLE_SCRIPTS_INITIALIZED = "sample_scripts_initialized";
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
     * 返回当前共享脚本目录。没有用户配置时使用默认目录。
     */
    public static File getScriptDirectory(Context context) {
        String savedPath = preferences(context).getString(KEY_SCRIPT_DIRECTORY_PATH, "");
        if (savedPath != null && !savedPath.trim().isEmpty()) {
            File savedDirectory = normalizeSharedStorageDirectory(new File(savedPath));
            if (savedDirectory != null) {
                return savedDirectory;
            }
        }
        return getDefaultScriptDirectory();
    }

    /**
     * 返回首次安装使用的默认脚本目录。
     */
    private static File getDefaultScriptDirectory() {
        return new File(
                new File(Environment.getExternalStorageDirectory(), STORAGE_ROOT_DIRECTORY_NAME),
                SCRIPTS_DIR_NAME
        );
    }

    /**
     * 返回便于用户识别的脚本目录路径，主共享存储统一显示为 /sdcard。
     */
    public static String getScriptDirectoryDisplayPath(Context context) {
        File directory = getScriptDirectory(context);
        File storageRoot = Environment.getExternalStorageDirectory();
        String directoryPath = directory.getAbsolutePath();
        String rootPath = storageRoot.getAbsolutePath();
        if (directoryPath.equals(rootPath)) {
            return "/sdcard";
        }
        if (directoryPath.startsWith(rootPath + File.separator)) {
            return "/sdcard" + directoryPath.substring(rootPath.length()).replace('\\', '/');
        }
        return directoryPath;
    }

    /**
     * 保存用户选择的脚本目录。仅接受主共享存储下的子目录，确保引擎和插件都能使用真实路径。
     */
    public static boolean setScriptDirectory(Context context, File directory) {
        File normalizedDirectory = normalizeSharedStorageDirectory(directory);
        File storageRoot = normalizeFile(Environment.getExternalStorageDirectory());
        if (normalizedDirectory == null
                || storageRoot == null
                || normalizedDirectory.equals(storageRoot)) {
            return false;
        }
        if (!normalizedDirectory.exists() && !normalizedDirectory.mkdirs()) {
            return false;
        }
        if (!normalizedDirectory.isDirectory()) {
            return false;
        }

        preferences(context)
                .edit()
                .putString(KEY_SCRIPT_DIRECTORY_PATH, normalizedDirectory.getAbsolutePath())
                .remove(KEY_SELECTED_SCRIPT_PATH)
                .apply();
        return true;
    }

    /**
     * 把用户输入的 /sdcard 或真实共享存储路径转为规范目录。
     */
    public static File resolveScriptDirectoryPath(String inputPath) {
        if (inputPath == null || inputPath.trim().isEmpty()) {
            return null;
        }

        String path = inputPath.trim().replace('\\', '/');
        if (path.equals("/sdcard")) {
            path = Environment.getExternalStorageDirectory().getAbsolutePath();
        } else if (path.startsWith("/sdcard/")) {
            path = Environment.getExternalStorageDirectory().getAbsolutePath()
                    + path.substring("/sdcard".length());
        }
        return normalizeSharedStorageDirectory(new File(path));
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
     * 创建共享脚本目录。内置示例只在当前安装第一次成功初始化目录时复制一次。
     *
     * @return 目录可直接读写时返回 true。
     */
    public static boolean ensureScriptDirectory(Context context) {
        if (!isScriptStorageAccessible(context)) {
            return false;
        }

        File scriptDirectory = getScriptDirectory(context);
        if (!scriptDirectory.exists() && !scriptDirectory.mkdirs()) {
            return false;
        }
        if (!scriptDirectory.isDirectory()) {
            return false;
        }

        SharedPreferences preferences = preferences(context);
        if (!preferences.getBoolean(KEY_SAMPLE_SCRIPTS_INITIALIZED, false)) {
            boolean initialized = true;
            for (String fileName : SAMPLE_SCRIPT_NAMES) {
                File targetFile = new File(scriptDirectory, fileName);
                if (!targetFile.exists() && !copyAssetScript(context, fileName, targetFile)) {
                    initialized = false;
                }
            }
            if (initialized) {
                preferences.edit().putBoolean(KEY_SAMPLE_SCRIPTS_INITIALIZED, true).apply();
            }
        }
        return true;
    }

    public static ScriptItem[] listScripts(Context context) {
        if (!ensureScriptDirectory(context)) {
            return new ScriptItem[0];
        }

        File[] files = getScriptDirectory(context).listFiles(File::isFile);
        if (files == null || files.length == 0) {
            return new ScriptItem[0];
        }

        Arrays.sort(files, (left, right) -> left.getName()
                .toLowerCase(Locale.US)
                .compareTo(right.getName().toLowerCase(Locale.US)));

        List<ScriptItem> items = new ArrayList<>();
        for (File file : files) {
            items.add(toScriptItem(context, file));
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

        String normalizedPath = normalizeSavedPath(context, path);
        ScriptItem[] scripts = listScripts(context);
        for (ScriptItem item : scripts) {
            if (item.filePath.equals(normalizedPath)) {
                return item;
            }
        }
        return null;
    }

    /**
     * 按真实路径读取共享存储中的脚本文件，不依赖跨进程的目录配置缓存。
     *
     * EngineService 使用该入口接收 App 或 IDE 已经选定的完整路径，目录切换后可以立即运行。
     */
    public static ScriptItem findSharedFileByPath(Context context, String path) {
        if (path == null || path.trim().isEmpty()) {
            return null;
        }

        File file = normalizeFile(new File(path.trim()));
        File storageRoot = normalizeFile(Environment.getExternalStorageDirectory());
        if (file == null || storageRoot == null || !file.isFile()) {
            return null;
        }
        String rootPrefix = storageRoot.getAbsolutePath() + File.separator;
        if (!file.getAbsolutePath().startsWith(rootPrefix)) {
            return null;
        }
        return toScriptItem(context, file);
    }

    public static String readScriptText(String filePath) throws IOException {
        try (FileInputStream inputStream = new FileInputStream(filePath)) {
            return readAllText(inputStream);
        }
    }

    private static ScriptItem toScriptItem(Context context, File file) {
        String fileName = file.getName();
        String language = detectLanguage(fileName);
        String description = readDescriptionPreview(file);
        String displayPath = getScriptDirectoryDisplayPath(context) + "/" + fileName;
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

    private static String normalizeSavedPath(Context context, String path) {
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
        return new File(getScriptDirectory(context), fileName).getAbsolutePath();
    }

    /**
     * 把一个内置示例直接写入共享目录。写入失败会删除半成品，避免列表显示损坏文件。
     */
    private static boolean copyAssetScript(Context context, String fileName, File targetFile) {
        String assetPath = ASSET_SCRIPT_DIR + "/" + fileName;
        try (InputStream inputStream = context.getAssets().open(assetPath);
             FileOutputStream outputStream = new FileOutputStream(targetFile)) {
            copyStream(inputStream, outputStream);
            return true;
        } catch (IOException ignored) {
            // 内置示例写入失败不影响用户已存在脚本的扫描。
            targetFile.delete();
            return false;
        }
    }

    /**
     * 规范化并校验目录必须位于主共享存储内，防止保存不可复用的内容提供器路径。
     */
    private static File normalizeSharedStorageDirectory(File directory) {
        File normalizedDirectory = normalizeFile(directory);
        File storageRoot = normalizeFile(Environment.getExternalStorageDirectory());
        if (normalizedDirectory == null || storageRoot == null) {
            return null;
        }
        String rootPrefix = storageRoot.getAbsolutePath() + File.separator;
        return normalizedDirectory.equals(storageRoot)
                || normalizedDirectory.getAbsolutePath().startsWith(rootPrefix)
                ? normalizedDirectory
                : null;
    }

    /**
     * 返回不含相对路径片段的规范文件路径。
     */
    private static File normalizeFile(File file) {
        if (file == null) {
            return null;
        }
        try {
            return file.getCanonicalFile();
        } catch (IOException exception) {
            return file.getAbsoluteFile();
        }
    }

    private static SharedPreferences preferences(Context context) {
        return context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE);
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
