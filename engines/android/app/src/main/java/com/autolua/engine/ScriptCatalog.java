/**
 * 文件用途：管理 App 私有脚本目录，负责扫描、复制示例和记录当前选择。
 */
package com.autolua.engine;

import android.content.Context;

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
import java.util.Comparator;
import java.util.List;
import java.util.Locale;

/**
 * App 侧脚本目录。
 *
 * 第一版把示例脚本从 assets/scripts 复制到 App 私有目录 files/scripts。
 * UI、悬浮窗和引擎服务都读取同一个目录和同一个选中脚本路径，后续接入
 * VS Code 同步、文件导入或用户脚本管理时，不需要再改悬浮窗运行入口。
 */
public final class ScriptCatalog {
    public static final String PREF_NAME = "script_state";
    public static final String KEY_SELECTED_SCRIPT_PATH = "selected_script_path";
    public static final String SCRIPTS_DIR_NAME = "scripts";
    public static final String DEFAULT_SCRIPT_FILE_NAME = "main.lua";

    private static final String ASSET_SCRIPT_DIR = "scripts";
    private static final int DESCRIPTION_READ_BYTES = 2048;
    private static final int MAX_DESCRIPTION_LENGTH = 42;
    private static final String[] SAMPLE_SCRIPT_NAMES = {
            "main.lua",
            "error.lua",
            "loop.lua",
            "input.lua",
            "screen.lua",
            "screen_benchmark.lua"
    };

    private ScriptCatalog() {
    }

    public static File getScriptDirectory(Context context) {
        return new File(context.getFilesDir(), SCRIPTS_DIR_NAME);
    }

    public static void ensureScriptDirectory(Context context) {
        File scriptDirectory = getScriptDirectory(context);
        if (!scriptDirectory.exists() && !scriptDirectory.mkdirs()) {
            return;
        }

        for (String fileName : SAMPLE_SCRIPT_NAMES) {
            File targetFile = new File(scriptDirectory, fileName);
            if (targetFile.exists()) {
                continue;
            }
            copyAssetScript(context, fileName, targetFile);
        }
    }

    public static ScriptItem[] listScripts(Context context) {
        ensureScriptDirectory(context);
        File[] files = getScriptDirectory(context).listFiles(File::isFile);
        if (files == null || files.length == 0) {
            return new ScriptItem[0];
        }

        Arrays.sort(files, Comparator.comparing(file -> file.getName().toLowerCase(Locale.US)));

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
        if (item == null) {
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

    public static String readScriptText(String filePath) throws IOException {
        try (FileInputStream inputStream = new FileInputStream(filePath)) {
            return readAllText(inputStream);
        }
    }

    private static ScriptItem toScriptItem(Context context, File file) {
        String fileName = file.getName();
        String language = detectLanguage(fileName);
        String description = readDescriptionPreview(file);
        String displayPath = getScriptDirectory(context).getName() + "/" + fileName;
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

        // 兼容旧版本保存的 assets 路径，例如 scripts/main.lua。
        String fileName = trimmed;
        int separatorIndex = Math.max(trimmed.lastIndexOf('/'), trimmed.lastIndexOf('\\'));
        if (separatorIndex >= 0 && separatorIndex + 1 < trimmed.length()) {
            fileName = trimmed.substring(separatorIndex + 1);
        }
        return new File(getScriptDirectory(context), fileName).getAbsolutePath();
    }

    private static void copyAssetScript(Context context, String fileName, File targetFile) {
        String assetPath = ASSET_SCRIPT_DIR + "/" + fileName;
        try (InputStream inputStream = context.getAssets().open(assetPath);
             FileOutputStream outputStream = new FileOutputStream(targetFile)) {
            byte[] buffer = new byte[4096];
            int readCount;
            while ((readCount = inputStream.read(buffer)) != -1) {
                outputStream.write(buffer, 0, readCount);
            }
        } catch (IOException ignored) {
            // 示例脚本缺失不影响用户脚本目录扫描，UI 会显示空目录状态。
        }
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
        if (lowerName.endsWith(".js")) {
            return "js";
        }
        if (lowerName.endsWith(".go")) {
            return "go";
        }
        return "text";
    }

    private static boolean isRunnableLanguage(String language) {
        return "lua".equals(language);
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
