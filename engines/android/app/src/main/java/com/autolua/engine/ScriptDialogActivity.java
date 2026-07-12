/**
 * 文件用途：在 App 主进程显示脚本原生弹窗、输入框、选择框和表单。
 */
package com.autolua.engine;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Bundle;
import android.os.Build;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.ArrayAdapter;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.LinkedHashMap;
import java.util.Locale;
import java.util.Map;

/**
 * 脚本原生弹窗 Activity。
 *
 * 该组件不在 :engine 进程，因此能稳定获得窗口 token、系统键盘和 Activity 生命周期。
 * 用户操作只会通过 ScriptUiEventDispatcher 发送 ui.event，Lua 侧仍在原脚本线程
 * 通过 m.ui.waitEvent 或 m.dialog.* 接收结果。
 */
public final class ScriptDialogActivity extends Activity {
    private long sessionId;
    private JSONObject spec;
    private AlertDialog dialog;
    private boolean eventSent;
    private boolean closedByEngine;
    private final Map<String, View> formFields = new LinkedHashMap<>();
    private final BroadcastReceiver commandReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            boolean closeAll = ScriptUiProtocol.ACTION_CLOSE_ALL.equals(action);
            boolean closeCurrent = ScriptUiProtocol.ACTION_CLOSE.equals(action)
                    && intent.getLongExtra(ScriptUiProtocol.EXTRA_SESSION_ID, 0) == sessionId;
            if (!closeAll && !closeCurrent) {
                return;
            }

            // 关闭命令来自 native 会话清理，此时 native 已经唤醒等待者，不再回传一条
            // 重复 close 事件。
            closedByEngine = true;
            eventSent = true;
            if (dialog != null && dialog.isShowing()) {
                dialog.dismiss();
            }
            finish();
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        sessionId = getIntent().getLongExtra(ScriptUiProtocol.EXTRA_SESSION_ID, 0);
        if (sessionId <= 0) {
            finish();
            return;
        }

        try {
            spec = new JSONObject(getIntent().getStringExtra(ScriptUiProtocol.EXTRA_SPEC_JSON));
        } catch (Exception exception) {
            dispatchEvent("error", makeData("message", "弹窗配置 JSON 无效"));
            finish();
            return;
        }

        registerCommandReceiver();
        showNativeDialog();
    }

    @Override
    protected void onDestroy() {
        unregisterCommandReceiver();
        // 系统主动销毁、用户按返回或 Activity 被替换时，向脚本明确通知页面已经关闭。
        if (!eventSent && !closedByEngine && !isChangingConfigurations()) {
            dispatchEvent("closed", JSONObject.NULL);
        }
        super.onDestroy();
    }

    private void registerCommandReceiver() {
        IntentFilter filter = new IntentFilter();
        filter.addAction(ScriptUiProtocol.ACTION_CLOSE);
        filter.addAction(ScriptUiProtocol.ACTION_CLOSE_ALL);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(commandReceiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            registerReceiver(commandReceiver, filter);
        }
    }

    private void unregisterCommandReceiver() {
        try {
            unregisterReceiver(commandReceiver);
        } catch (IllegalArgumentException ignored) {
            // onCreate 在解析失败后可能在注册前结束；此时无需额外处理。
        }
    }

    /**
     * 按 type 渲染原生 AlertDialog。支持 alert、confirm、input、select、form。
     */
    private void showNativeDialog() {
        String type = spec.optString("type", "alert").toLowerCase(Locale.US);
        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        builder.setTitle(spec.optString("title", "提示"));

        switch (type) {
            case "confirm":
                configureConfirmDialog(builder);
                break;
            case "input":
                configureInputDialog(builder);
                break;
            case "select":
                configureSelectDialog(builder);
                break;
            case "form":
                configureFormDialog(builder);
                break;
            case "alert":
            default:
                configureAlertDialog(builder);
                break;
        }

        dialog = builder.create();
        boolean cancelable = spec.optBoolean("cancelable", true);
        dialog.setCancelable(cancelable);
        dialog.setCanceledOnTouchOutside(cancelable);
        dialog.setOnCancelListener(ignored -> sendAndFinish("cancel", makeData("value", false)));
        dialog.setOnDismissListener(ignored -> {
            if (!isFinishing()) {
                finish();
            }
        });
        dialog.show();
    }

    private void configureAlertDialog(AlertDialog.Builder builder) {
        builder.setMessage(spec.optString("message", ""));
        builder.setPositiveButton(spec.optString("positiveText", "确定"),
                (ignored, which) -> sendAndFinish("confirm", makeData("value", true)));
    }

    private void configureConfirmDialog(AlertDialog.Builder builder) {
        builder.setMessage(spec.optString("message", ""));
        builder.setPositiveButton(spec.optString("positiveText", "确定"),
                (ignored, which) -> sendAndFinish("confirm", makeData("value", true)));
        builder.setNegativeButton(spec.optString("negativeText", "取消"),
                (ignored, which) -> sendAndFinish("cancel", makeData("value", false)));
    }

    private void configureInputDialog(AlertDialog.Builder builder) {
        builder.setMessage(spec.optString("message", ""));
        EditText input = new EditText(this);
        input.setHint(spec.optString("hint", ""));
        input.setText(spec.optString("defaultText", ""));
        // TextView.isSingleLine 在部分 Android 版本是隐藏 API，不能再读回 View 状态；
        // 直接使用配置值同时设置 singleLine 和 inputType，避免输入弹窗启动时崩溃。
        boolean multiline = spec.optBoolean("multiline", false);
        input.setSingleLine(!multiline);
        input.setInputType(resolveInputType(spec.optString("inputType", "text"), !multiline));
        input.setSelectAllOnFocus(spec.optBoolean("selectAll", false));
        int padding = dp(20);
        input.setPadding(padding, dp(4), padding, dp(4));
        builder.setView(input);
        builder.setPositiveButton(spec.optString("positiveText", "确定"),
                (ignored, which) -> sendAndFinish("confirm", makeData("value", input.getText().toString())));
        builder.setNegativeButton(spec.optString("negativeText", "取消"),
                (ignored, which) -> sendAndFinish("cancel", makeData("value", false)));
    }

    private void configureSelectDialog(AlertDialog.Builder builder) {
        builder.setMessage(spec.optString("message", ""));
        String[] items = readStringArray(spec.optJSONArray("items"));
        if (items.length == 0) {
            builder.setMessage("没有可选项");
            builder.setPositiveButton(spec.optString("positiveText", "确定"),
                    (ignored, which) -> sendAndFinish("confirm", makeSelectionData(0, "")));
            return;
        }

        int initialIndex = clampIndex(spec.optInt("selectedIndex", 1) - 1, items.length);
        final int[] selectedIndex = {initialIndex};
        builder.setSingleChoiceItems(items, initialIndex, (ignored, which) -> selectedIndex[0] = which);
        builder.setPositiveButton(spec.optString("positiveText", "确定"),
                (ignored, which) -> sendAndFinish(
                        "confirm",
                        makeSelectionData(selectedIndex[0] + 1, items[selectedIndex[0]])
                ));
        builder.setNegativeButton(spec.optString("negativeText", "取消"),
                (ignored, which) -> sendAndFinish("cancel", makeData("value", false)));
    }

    private void configureFormDialog(AlertDialog.Builder builder) {
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
                    addFormField(content, field, index);
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
                (ignored, which) -> sendAndFinish("confirm", collectFormData()));
        builder.setNegativeButton(spec.optString("negativeText", "取消"),
                (ignored, which) -> sendAndFinish("cancel", makeData("value", false)));
    }

    /**
     * 构建一个表单字段。字段 type 支持 text、password、number、multiline、boolean、select、label。
     */
    private void addFormField(LinearLayout content, JSONObject field, int fieldIndex) {
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
            formFields.put(name, checkBox);
            return;
        }

        if ("select".equals(type)) {
            Spinner spinner = new Spinner(this);
            String[] items = readStringArray(field.optJSONArray("items"));
            ArrayAdapter<String> adapter = new ArrayAdapter<>(
                    this,
                    android.R.layout.simple_spinner_dropdown_item,
                    items
            );
            spinner.setAdapter(adapter);
            if (items.length > 0) {
                spinner.setSelection(clampIndex(field.optInt("selectedIndex", 1) - 1, items.length));
            }
            content.addView(spinner, matchWidthWrapContent());
            formFields.put(name, spinner);
            return;
        }

        EditText input = new EditText(this);
        input.setHint(field.optString("hint", ""));
        input.setText(field.optString("default", field.optString("defaultText", "")));
        boolean multiline = "multiline".equals(type);
        input.setSingleLine(!multiline);
        input.setInputType(resolveInputType(type, !multiline));
        content.addView(input, matchWidthWrapContent());
        formFields.put(name, input);
    }

    private JSONObject collectFormData() {
        JSONObject values = new JSONObject();
        for (Map.Entry<String, View> entry : formFields.entrySet()) {
            Object value = readFieldValue(entry.getValue());
            try {
                values.put(entry.getKey(), value == null ? JSONObject.NULL : value);
            } catch (JSONException ignored) {
                // 不让单个字段序列化失败阻断整个表单提交。
            }
        }
        return makeData("values", values);
    }

    private Object readFieldValue(View view) {
        if (view instanceof EditText) {
            return ((EditText) view).getText().toString();
        }
        if (view instanceof CheckBox) {
            return ((CheckBox) view).isChecked();
        }
        if (view instanceof Spinner) {
            return ((Spinner) view).getSelectedItem();
        }
        return null;
    }

    private void sendAndFinish(String event, Object data) {
        if (eventSent) {
            return;
        }
        eventSent = true;
        dispatchEvent(event, data);
        if (dialog != null && dialog.isShowing()) {
            dialog.dismiss();
        }
        finish();
    }

    private void dispatchEvent(String event, Object data) {
        ScriptUiEventDispatcher.dispatch(this, sessionId, event, data);
    }

    private static JSONObject makeData(String key, Object value) {
        JSONObject data = new JSONObject();
        try {
            data.put(key, value == null ? JSONObject.NULL : value);
        } catch (JSONException ignored) {
            // JSONObject 对单个基础值不会失败；保留空对象可让脚本继续处理事件。
        }
        return data;
    }

    private static JSONObject makeSelectionData(int index, String value) {
        JSONObject data = new JSONObject();
        try {
            data.put("index", index);
            data.put("value", value);
        } catch (JSONException ignored) {
            // 基础值写入不会失败。
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

    private LinearLayout.LayoutParams matchWidthWrapContent() {
        return new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
        );
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
