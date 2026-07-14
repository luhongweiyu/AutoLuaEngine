/**
 * 文件用途：在 App 主进程通过 WindowManager 显示脚本原生对话框和表单。
 */
package com.xiaoyv.engine;

import android.app.AlertDialog;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.IBinder;
import android.provider.Settings;
import android.text.InputType;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.Locale;
import java.util.Map;

/**
 * 脚本对话框悬浮服务。
 *
 * 旧项目的脚本 UI 直接通过 WindowManager.addView 创建 TYPE_APPLICATION_OVERLAY 窗口，
 * 并设置 FLAG_NOT_TOUCH_MODAL，让窗口范围外的触摸继续交给下方应用。本服务沿用这个
 * 窗口模型：对话框本身保持原生控件和输入能力，框外不产生暗幕、不关闭对话框，也不拦截
 * 原页面的触摸。
 *
 * Lua 运行在 :engine 进程。用户点击确认、取消或提交表单后，本服务仅投递 ui.event，
 * 不会在 Android UI 线程直接执行 Lua。
 */
public final class ScriptDialogOverlayService extends Service {
    public static final String ACTION_SHOW = "com.xiaoyv.engine.action.SCRIPT_DIALOG_SHOW";
    public static final String ACTION_CLOSE = "com.xiaoyv.engine.action.SCRIPT_DIALOG_CLOSE";
    public static final String ACTION_CLOSE_ALL = "com.xiaoyv.engine.action.SCRIPT_DIALOG_CLOSE_ALL";

    private final Map<Long, DialogEntry> entries = new HashMap<>();

    /**
     * 从任意应用进程向主进程服务发送对话框命令。
     *
     * 只有创建窗口时才检查悬浮窗权限。关闭命令即使权限后来被撤销也必须能够送达，
     * 以便脚本停止时及时清理已经存在的窗口。
     */
    public static boolean sendCommand(Context context, String action, long sessionId, String specJson) {
        if (context == null || action == null || action.isEmpty()) {
            return false;
        }
        if (ACTION_SHOW.equals(action)
                && Build.VERSION.SDK_INT >= Build.VERSION_CODES.M
                && !Settings.canDrawOverlays(context)) {
            return false;
        }

        Intent intent = new Intent(context, ScriptDialogOverlayService.class);
        intent.setAction(action);
        intent.putExtra(ScriptUiProtocol.EXTRA_SESSION_ID, sessionId);
        intent.putExtra(ScriptUiProtocol.EXTRA_SPEC_JSON, specJson == null ? "{}" : specJson);
        try {
            context.startService(intent);
            return true;
        } catch (RuntimeException exception) {
            return false;
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent == null || intent.getAction() == null) {
            return START_NOT_STICKY;
        }

        String action = intent.getAction();
        long sessionId = intent.getLongExtra(ScriptUiProtocol.EXTRA_SESSION_ID, 0);
        if (ACTION_SHOW.equals(action)) {
            showDialog(sessionId, readSpec(intent));
        } else if (ACTION_CLOSE.equals(action)) {
            closeDialogFromEngine(sessionId);
        } else if (ACTION_CLOSE_ALL.equals(action)) {
            closeAllDialogsFromEngine();
        }
        return START_NOT_STICKY;
    }

    @Override
    public void onDestroy() {
        // Service 被系统或引擎清理时，native 会话已经负责唤醒脚本；这里仅回收窗口，
        // 不再发送重复的 closed 事件。
        closeAllDialogsFromEngine();
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    /**
     * 解析来自 native UI 会话的配置。无效 JSON 会按空配置显示，并由脚本通过默认按钮
     * 正常退出，避免界面服务因一份错误配置崩溃。
     */
    private JSONObject readSpec(Intent intent) {
        try {
            String specJson = intent.getStringExtra(ScriptUiProtocol.EXTRA_SPEC_JSON);
            return new JSONObject(specJson == null ? "{}" : specJson);
        } catch (JSONException exception) {
            return new JSONObject();
        }
    }

    /**
     * 创建一个独立 overlay 对话框。重复 sessionId 只可能来自同一会话的重新打开，
     * 此时先静默移除旧窗口，保证屏幕上最多保留一个对应窗口。
     */
    private void showDialog(long sessionId, JSONObject spec) {
        if (sessionId <= 0) {
            return;
        }

        // 新窗口紧接着就会加入 entries；替换旧窗口期间不能 stopSelf，否则 Android
        // 可能在新 Dialog.show() 后立即销毁本 Service 并把新窗口一并关闭。
        closeDialogFromEngine(sessionId, false);
        DialogEntry entry = new DialogEntry(sessionId);
        try {
            createNativeDialog(entry, spec);
            entries.put(sessionId, entry);
            configureOverlayWindow(entry.dialog.getWindow());
            entry.dialog.show();
        } catch (RuntimeException exception) {
            entries.remove(sessionId);
            entry.closedByEngine = true;
            dismissQuietly(entry);
            ScriptUiEventDispatcher.dispatch(this, sessionId, "error", makeData(
                    "message",
                    "对话框创建失败：" + exception.getMessage()
            ));
            stopWhenIdle();
        }
    }

    /**
     * 使用系统原生 AlertDialog 控件构造具体内容。
     *
     * Builder 只负责布局和控件样式；真正使它能覆盖在任意应用之上的关键窗口类型和
     * FLAG_NOT_TOUCH_MODAL 在 configureOverlayWindow 中统一设置。
     */
    private void createNativeDialog(DialogEntry entry, JSONObject spec) {
        String type = spec.optString("type", "alert").toLowerCase(Locale.US);
        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        builder.setTitle(spec.optString("title", "提示"));

        switch (type) {
            case "confirm":
                configureConfirmDialog(entry, builder, spec);
                break;
            case "input":
                configureInputDialog(entry, builder, spec);
                break;
            case "select":
                configureSelectDialog(entry, builder, spec);
                break;
            case "form":
                configureFormDialog(entry, builder, spec);
                break;
            case "alert":
            default:
                configureAlertDialog(entry, builder, spec);
                break;
        }

        entry.dialog = builder.create();
        // 对话框的关闭结果必须由脚本提供的按钮决定。框外触摸会直接穿透，返回键也
        // 不应意外结束脚本等待中的 UI 会话。
        entry.dialog.setCancelable(false);
        entry.dialog.setCanceledOnTouchOutside(false);
        entry.dialog.setOnKeyListener((ignored, keyCode, event) -> keyCode == KeyEvent.KEYCODE_BACK);
        entry.dialog.setOnCancelListener(ignored -> completeDialog(
                entry,
                "cancel",
                makeData("value", false)
        ));
        entry.dialog.setOnDismissListener(ignored -> onDialogDismissed(entry));
    }

    /**
     * 将原生 Dialog 的 Window 改为悬浮窗口。
     *
     * TYPE_APPLICATION_OVERLAY 让 Service Context 可以显示 Dialog，无需启动 Activity；
     * FLAG_NOT_TOUCH_MODAL 是旧项目 UI 使用的核心标志，窗口外的点击会交给下方应用。
     * 不能设置 FLAG_NOT_FOCUSABLE，否则输入框无法调起软键盘并接收文字。
     */
    private void configureOverlayWindow(Window dialogWindow) {
        if (dialogWindow == null) {
            throw new IllegalStateException("原生对话框窗口为空");
        }

        int windowType = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                : WindowManager.LayoutParams.TYPE_PHONE;
        dialogWindow.setType(windowType);
        dialogWindow.setGravity(Gravity.CENTER);
        dialogWindow.clearFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
        dialogWindow.addFlags(WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL);
        dialogWindow.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);

        // 一些系统版本会保留主题中的默认 dimAmount；同时清标志和置零可确保不出现暗幕。
        WindowManager.LayoutParams attributes = dialogWindow.getAttributes();
        attributes.dimAmount = 0F;
        dialogWindow.setAttributes(attributes);
    }

    private void configureAlertDialog(
            DialogEntry entry,
            AlertDialog.Builder builder,
            JSONObject spec
    ) {
        builder.setMessage(spec.optString("message", ""));
        builder.setPositiveButton(spec.optString("positiveText", "确定"),
                (ignored, which) -> completeDialog(entry, "confirm", makeData("value", true)));
    }

    private void configureConfirmDialog(
            DialogEntry entry,
            AlertDialog.Builder builder,
            JSONObject spec
    ) {
        builder.setMessage(spec.optString("message", ""));
        builder.setPositiveButton(spec.optString("positiveText", "确定"),
                (ignored, which) -> completeDialog(entry, "confirm", makeData("value", true)));
        builder.setNegativeButton(spec.optString("negativeText", "取消"),
                (ignored, which) -> completeDialog(entry, "cancel", makeData("value", false)));
    }

    private void configureInputDialog(
            DialogEntry entry,
            AlertDialog.Builder builder,
            JSONObject spec
    ) {
        builder.setMessage(spec.optString("message", ""));
        EditText input = new EditText(this);
        input.setHint(spec.optString("hint", ""));
        input.setText(spec.optString("defaultText", ""));
        boolean multiline = spec.optBoolean("multiline", false);
        input.setSingleLine(!multiline);
        input.setInputType(resolveInputType(spec.optString("inputType", "text"), !multiline));
        input.setSelectAllOnFocus(spec.optBoolean("selectAll", false));
        int horizontalPadding = dp(20);
        input.setPadding(horizontalPadding, dp(4), horizontalPadding, dp(4));
        builder.setView(input);
        builder.setPositiveButton(spec.optString("positiveText", "确定"),
                (ignored, which) -> completeDialog(
                        entry,
                        "confirm",
                        makeData("value", input.getText().toString())
                ));
        builder.setNegativeButton(spec.optString("negativeText", "取消"),
                (ignored, which) -> completeDialog(entry, "cancel", makeData("value", false)));
    }

    private void configureSelectDialog(
            DialogEntry entry,
            AlertDialog.Builder builder,
            JSONObject spec
    ) {
        String[] items = readStringArray(spec.optJSONArray("items"));
        if (items.length == 0) {
            String message = spec.optString("message", "");
            builder.setMessage(message.isEmpty() ? "没有可选项" : message + "\n没有可选项");
            builder.setPositiveButton(spec.optString("positiveText", "确定"),
                    (ignored, which) -> completeDialog(
                            entry,
                            "confirm",
                            makeSelectionData(0, "")
                    ));
            return;
        }

        int initialIndex = clampIndex(spec.optInt("selectedIndex", 1) - 1, items.length);
        final int[] selectedIndex = {initialIndex};

        // AlertDialog 的 message 和 setSingleChoiceItems 在部分系统样式中只能二选一：
        // 只要 message 为空字符串而非 null，列表就会被内容面板替换。这里自行组合说明
        // 文字和 RadioGroup，确保 options.message 与全部选项始终同时可见。
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(dp(20), dp(4), dp(20), dp(4));
        String message = spec.optString("message", "");
        if (!message.isEmpty()) {
            TextView messageView = new TextView(this);
            messageView.setText(message);
            messageView.setTextSize(14);
            LinearLayout.LayoutParams messageParams = matchWidthWrapContent();
            messageParams.bottomMargin = dp(6);
            content.addView(messageView, messageParams);
        }

        RadioGroup group = new RadioGroup(this);
        group.setOrientation(RadioGroup.VERTICAL);
        for (int index = 0; index < items.length; index++) {
            RadioButton itemView = new RadioButton(this);
            itemView.setId(View.generateViewId());
            itemView.setText(items[index]);
            itemView.setChecked(index == initialIndex);
            group.addView(itemView, matchWidthWrapContent());
        }
        group.setOnCheckedChangeListener((ignored, checkedId) -> {
            for (int index = 0; index < group.getChildCount(); index++) {
                if (group.getChildAt(index).getId() == checkedId) {
                    selectedIndex[0] = index;
                    return;
                }
            }
        });

        ScrollView scrollView = new ScrollView(this);
        scrollView.addView(content, new ScrollView.LayoutParams(
                ScrollView.LayoutParams.MATCH_PARENT,
                ScrollView.LayoutParams.WRAP_CONTENT
        ));
        content.addView(group, matchWidthWrapContent());
        builder.setView(scrollView);
        builder.setPositiveButton(spec.optString("positiveText", "确定"),
                (ignored, which) -> completeDialog(
                        entry,
                        "confirm",
                        makeSelectionData(selectedIndex[0] + 1, items[selectedIndex[0]])
                ));
        builder.setNegativeButton(spec.optString("negativeText", "取消"),
                (ignored, which) -> completeDialog(entry, "cancel", makeData("value", false)));
    }

    private void configureFormDialog(
            DialogEntry entry,
            AlertDialog.Builder builder,
            JSONObject spec
    ) {
        String message = spec.optString("message", "");
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(dp(20), dp(4), dp(20), dp(4));

        if (!message.isEmpty()) {
            TextView messageView = new TextView(this);
            messageView.setText(message);
            messageView.setTextSize(14);
            content.addView(messageView, matchWidthWrapContent());
        }

        JSONArray fields = spec.optJSONArray("fields");
        if (fields != null) {
            for (int index = 0; index < fields.length(); index++) {
                JSONObject field = fields.optJSONObject(index);
                if (field != null) {
                    addFormField(entry, content, field, index);
                }
            }
        }

        ScrollView scrollView = new ScrollView(this);
        scrollView.addView(content, new ScrollView.LayoutParams(
                ScrollView.LayoutParams.MATCH_PARENT,
                ScrollView.LayoutParams.WRAP_CONTENT
        ));
        builder.setView(scrollView);
        builder.setPositiveButton(spec.optString("positiveText", "确定"),
                (ignored, which) -> completeDialog(entry, "confirm", collectFormData(entry)));
        builder.setNegativeButton(spec.optString("negativeText", "取消"),
                (ignored, which) -> completeDialog(entry, "cancel", makeData("value", false)));
    }

    /**
     * 构建单个表单字段。字段类型沿用现有 Lua API：text、password、number、multiline、
     * boolean、select、label；所有字段 View 保存在当前会话 entry，确认时再一次性读取。
     */
    private void addFormField(
            DialogEntry entry,
            LinearLayout content,
            JSONObject field,
            int fieldIndex
    ) {
        String type = field.optString("type", "text").toLowerCase(Locale.US);
        String name = field.optString("name", "field" + (fieldIndex + 1));
        String label = field.optString("label", name);

        if (!label.isEmpty()) {
            TextView labelView = new TextView(this);
            labelView.setText(label);
            labelView.setTextSize(14);
            LinearLayout.LayoutParams labelParams = matchWidthWrapContent();
            labelParams.topMargin = dp(10);
            content.addView(labelView, labelParams);
        }

        if ("label".equals(type)) {
            return;
        }

        if ("boolean".equals(type) || "switch".equals(type) || "checkbox".equals(type)) {
            CheckBox checkBox = new CheckBox(this);
            checkBox.setText(field.optString("text", ""));
            checkBox.setChecked(field.optBoolean("default", false));
            content.addView(checkBox, matchWidthWrapContent());
            entry.formFields.put(name, checkBox);
            return;
        }

        if ("select".equals(type)) {
            String[] items = readStringArray(field.optJSONArray("items"));
            RadioGroup group = new RadioGroup(this);
            group.setOrientation(RadioGroup.VERTICAL);
            int selectedIndex = clampIndex(field.optInt("selectedIndex", 1) - 1, items.length);
            for (int index = 0; index < items.length; index++) {
                RadioButton itemView = new RadioButton(this);
                itemView.setId(View.generateViewId());
                itemView.setText(items[index]);
                itemView.setChecked(index == selectedIndex);
                group.addView(itemView, matchWidthWrapContent());
            }

            // Spinner 的下拉列表会额外申请 TYPE_APPLICATION_SUB_PANEL；该窗口类型不能
            // 附着到 TYPE_APPLICATION_OVERLAY。内嵌 RadioGroup 不创建第二个窗口，
            // 并且与 m.dialog.select 保持一致，能稳定工作在任意前台应用上方。
            content.addView(group, matchWidthWrapContent());
            entry.formFields.put(name, group);
            return;
        }

        EditText input = new EditText(this);
        input.setHint(field.optString("hint", ""));
        input.setText(field.optString("default", field.optString("defaultText", "")));
        boolean multiline = "multiline".equals(type);
        input.setSingleLine(!multiline);
        input.setInputType(resolveInputType(type, !multiline));
        content.addView(input, matchWidthWrapContent());
        entry.formFields.put(name, input);
    }

    private JSONObject collectFormData(DialogEntry entry) {
        JSONObject values = new JSONObject();
        for (Map.Entry<String, View> field : entry.formFields.entrySet()) {
            try {
                Object value = readFieldValue(field.getValue());
                values.put(field.getKey(), value == null ? JSONObject.NULL : value);
            } catch (JSONException ignored) {
                // 单个字段序列化失败不应阻断整个表单提交；其他有效字段仍可返回给脚本。
            }
        }
        return makeData("values", values);
    }

    private static Object readFieldValue(View view) {
        if (view instanceof EditText) {
            return ((EditText) view).getText().toString();
        }
        if (view instanceof CheckBox) {
            return ((CheckBox) view).isChecked();
        }
        if (view instanceof RadioGroup) {
            RadioGroup group = (RadioGroup) view;
            int checkedId = group.getCheckedRadioButtonId();
            View checkedView = group.findViewById(checkedId);
            if (checkedView instanceof RadioButton) {
                return ((RadioButton) checkedView).getText().toString();
            }
        }
        return null;
    }

    /**
     * 将用户操作转成单次 UI 事件，并在回调前从窗口表移除，防止 AlertDialog 自动 dismiss
     * 再触发一次 closed 事件。
     */
    private void completeDialog(DialogEntry entry, String event, Object data) {
        if (entry.eventSent || entry.closedByEngine) {
            return;
        }

        entry.eventSent = true;
        entries.remove(entry.sessionId);
        dismissQuietly(entry);
        ScriptUiEventDispatcher.dispatch(this, entry.sessionId, event, data);
        stopWhenIdle();
    }

    /**
     * 处理系统主动移除窗口的情况。正常按钮路径和引擎清理路径都会提前设置状态，因此
     * 只有非预期 dismiss 才向脚本报告 closed。
     */
    private void onDialogDismissed(DialogEntry entry) {
        if (entry.eventSent || entry.closedByEngine) {
            return;
        }

        entry.eventSent = true;
        entries.remove(entry.sessionId);
        ScriptUiEventDispatcher.dispatch(this, entry.sessionId, "closed", JSONObject.NULL);
        stopWhenIdle();
    }

    /**
     * native 会话关闭时静默移除对应窗口。native 已经唤醒 waitEvent，不能再额外投递
     * closed，否则下一次复用会话 ID 时可能收到过期事件。
     */
    private void closeDialogFromEngine(long sessionId) {
        closeDialogFromEngine(sessionId, true);
    }

    /**
     * 静默关闭一个窗口。showDialog 替换同会话窗口时会传 false，延后到新窗口登记后
     * 再判断 Service 是否空闲；其他引擎关闭路径则立即回收空闲 Service。
     */
    private void closeDialogFromEngine(long sessionId, boolean stopWhenIdle) {
        if (sessionId <= 0) {
            return;
        }

        DialogEntry entry = entries.remove(sessionId);
        if (entry == null) {
            return;
        }
        entry.closedByEngine = true;
        dismissQuietly(entry);
        if (stopWhenIdle) {
            stopWhenIdle();
        }
    }

    private void closeAllDialogsFromEngine() {
        DialogEntry[] activeEntries = entries.values().toArray(new DialogEntry[0]);
        entries.clear();
        for (DialogEntry entry : activeEntries) {
            entry.closedByEngine = true;
            dismissQuietly(entry);
        }
        stopWhenIdle();
    }

    private static void dismissQuietly(DialogEntry entry) {
        if (entry.dialog != null && entry.dialog.isShowing()) {
            entry.dialog.dismiss();
        }
    }

    private void stopWhenIdle() {
        if (entries.isEmpty()) {
            stopSelf();
        }
    }

    private static JSONObject makeData(String key, Object value) {
        JSONObject data = new JSONObject();
        try {
            data.put(key, value == null ? JSONObject.NULL : value);
        } catch (JSONException ignored) {
            // 单个基础值无法写入时保留空对象，脚本仍能收到对应事件类型。
        }
        return data;
    }

    private static JSONObject makeSelectionData(int index, String value) {
        JSONObject data = new JSONObject();
        try {
            data.put("index", index);
            data.put("value", value);
        } catch (JSONException ignored) {
            // index 和 value 都是基础值，不会阻断事件投递。
        }
        return data;
    }

    private static String[] readStringArray(JSONArray array) {
        if (array == null || array.length() == 0) {
            return new String[0];
        }

        String[] values = new String[array.length()];
        for (int index = 0; index < array.length(); index++) {
            values[index] = String.valueOf(array.opt(index));
        }
        return values;
    }

    private static int clampIndex(int index, int length) {
        if (length <= 0) {
            return 0;
        }
        return Math.max(0, Math.min(index, length - 1));
    }

    private static int resolveInputType(String type, boolean singleLine) {
        int inputType;
        switch (type == null ? "" : type.toLowerCase(Locale.US)) {
            case "number":
                inputType = InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_FLAG_SIGNED
                        | InputType.TYPE_NUMBER_FLAG_DECIMAL;
                break;
            case "password":
                inputType = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD;
                break;
            default:
                inputType = InputType.TYPE_CLASS_TEXT;
                if (!singleLine) {
                    inputType |= InputType.TYPE_TEXT_FLAG_MULTI_LINE;
                }
                break;
        }
        return inputType;
    }

    private static LinearLayout.LayoutParams matchWidthWrapContent() {
        return new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
        );
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    /**
     * 每个 native UI session 对应一个独立 DialogEntry，表单字段绝不跨会话共享，
     * 使多 Lua 线程同时等待各自对话框时也不会串数据。
     */
    private static final class DialogEntry {
        private final long sessionId;
        private final Map<String, View> formFields = new LinkedHashMap<>();
        private AlertDialog dialog;
        private boolean eventSent;
        private boolean closedByEngine;

        private DialogEntry(long sessionId) {
            this.sessionId = sessionId;
        }
    }
}
