package com.autolua.engine;

import android.content.Context;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

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
    private static final String[] SAMPLE_SCRIPT_NAMES = {
            "main.lua",
            "error.lua",
            "loop.lua",
            "touch.lua",
            "screen.lua",
            "screen_benchmark.lua"
    };
    private static final Map<String, String> SAMPLE_DESCRIPTIONS = makeSampleDescriptions();

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
        File[] files = getScriptDirectory(context).listFiles(file ->
                file.isFile() && file.getName().toLowerCase(Locale.US).endsWith(".lua")
        );
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
        String description = SAMPLE_DESCRIPTIONS.containsKey(fileName)
                ? SAMPLE_DESCRIPTIONS.get(fileName)
                : "用户脚本";
        String displayPath = getScriptDirectory(context).getName() + "/" + fileName;
        return new ScriptItem(
                fileName,
                file.getAbsolutePath(),
                displayPath,
                description,
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

    private static Map<String, String> makeSampleDescriptions() {
        Map<String, String> descriptions = new HashMap<>();
        descriptions.put("main.lua", "Lua、文件、命名空间和中文标识符");
        descriptions.put("error.lua", "验证脚本错误返回");
        descriptions.put("loop.lua", "验证长循环和停止");
        descriptions.put("touch.lua", "验证点击、滑动和无障碍状态");
        descriptions.put("screen.lua", "验证截图句柄和取色");
        descriptions.put("screen_benchmark.lua", "连续截图、释放图片句柄并统计耗时");
        return descriptions;
    }

    public static final class ScriptItem {
        public final String fileName;
        public final String filePath;
        public final String displayPath;
        public final String description;
        public final long sizeBytes;
        public final long modifiedAt;

        private ScriptItem(
                String fileName,
                String filePath,
                String displayPath,
                String description,
                long sizeBytes,
                long modifiedAt) {
            this.fileName = fileName;
            this.filePath = filePath;
            this.displayPath = displayPath;
            this.description = description;
            this.sizeBytes = sizeBytes;
            this.modifiedAt = modifiedAt;
        }
    }
}
