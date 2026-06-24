package com.autolua.engine;

import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;

/**
 * Android 剪贴板桥。
 *
 * 第一版只提供写入文本能力，配合 root `input keyevent KEYCODE_PASTE`
 * 实现复杂文本输入。读取剪贴板在 Android 新版本限制较多，后续有明确需求再补。
 */
public final class ClipboardBridge {
    private static final String CLIP_LABEL = "AutoLuaEngine";

    private ClipboardBridge() {
    }

    public static boolean setText(Context context, String text) {
        if (context == null || text == null) {
            return false;
        }

        Object service = context.getSystemService(Context.CLIPBOARD_SERVICE);
        if (!(service instanceof ClipboardManager)) {
            return false;
        }

        try {
            ClipboardManager clipboardManager = (ClipboardManager) service;
            clipboardManager.setPrimaryClip(ClipData.newPlainText(CLIP_LABEL, text));
            return true;
        } catch (RuntimeException exception) {
            return false;
        }
    }
}
