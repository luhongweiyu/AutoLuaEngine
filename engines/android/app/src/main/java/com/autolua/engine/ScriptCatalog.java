package com.autolua.engine;

/**
 * 内置脚本清单。
 *
 * 第一版脚本仍放在 assets/scripts 里，App 和悬浮窗都从这里读取同一份列表，
 * 避免两个入口显示的脚本不一致。后续接入本地脚本目录时，可以把这里替换成
 * 文件扫描结果。
 */
public final class ScriptCatalog {
    public static final String PREF_NAME = "script_state";
    public static final String KEY_SELECTED_SCRIPT_PATH = "selected_script_path";
    public static final String DEFAULT_SCRIPT_PATH = "scripts/main.lua";

    private static final ScriptItem[] BUILTIN_SCRIPTS = {
            new ScriptItem("基础验证", "scripts/main.lua", "Lua、文件、命名空间和中文标识符"),
            new ScriptItem("错误验证", "scripts/error.lua", "验证脚本错误返回"),
            new ScriptItem("循环停止", "scripts/loop.lua", "验证长循环和停止"),
            new ScriptItem("触控验证", "scripts/touch.lua", "验证点击、滑动和无障碍状态"),
            new ScriptItem("截图验证", "scripts/screen.lua", "验证截图句柄和取色"),
            new ScriptItem("截图压测", "scripts/screen_benchmark.lua", "连续截图、释放图片句柄并统计耗时")
    };

    private ScriptCatalog() {
    }

    public static ScriptItem[] builtInScripts() {
        return BUILTIN_SCRIPTS.clone();
    }

    public static ScriptItem findByPath(String assetPath) {
        for (ScriptItem item : BUILTIN_SCRIPTS) {
            if (item.assetPath.equals(assetPath)) {
                return item;
            }
        }
        return BUILTIN_SCRIPTS[0];
    }

    public static final class ScriptItem {
        public final String title;
        public final String assetPath;
        public final String description;

        private ScriptItem(String title, String assetPath, String description) {
            this.title = title;
            this.assetPath = assetPath;
            this.description = description;
        }
    }
}
