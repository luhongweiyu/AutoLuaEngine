package com.autolua.engine;

import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/**
 * Android 引擎主界面。
 *
 * 当前先提供脚本列表、运行控制、截图授权和悬浮窗入口。脚本编辑仍放在
 * VS Code/PC 侧，App 侧负责运行和权限状态。
 */
public final class MainActivity extends Activity {
    public static final String ACTION_REQUEST_SCREEN_CAPTURE =
            "com.autolua.engine.action.REQUEST_SCREEN_CAPTURE";

    private static final int REQUEST_SCREEN_CAPTURE = 1001;
    private static final int REQUEST_OVERLAY_PERMISSION = 1002;
    private static final int ROOT_PADDING = 32;
    private static final int SECTION_MARGIN = 24;
    private static final int ITEM_MARGIN = 10;

    private TextView outputView;
    private TextView selectedScriptView;
    private Button runButton;
    private Button errorButton;
    private Button loopButton;
    private Button touchButton;
    private Button captureButton;
    private Button screenButton;
    private Button stopButton;
    private Button floatingButton;
    private ScriptCatalog.ScriptItem selectedScript;
    private final BroadcastReceiver engineStatusReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            handleEngineStatus(intent);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        ensureAppFilesDir();
        ScreenCaptureBridge.init(getApplicationContext());
        NativeEngine.init(getApplicationContext());
        EngineHttpServer.start(getApplicationContext());
        selectedScript = loadSelectedScript();
        setContentView(createContentView());
        handleIntent(getIntent());
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        handleIntent(intent);
    }

    @Override
    protected void onStart() {
        super.onStart();
        IntentFilter filter = new IntentFilter(EngineService.ACTION_STATUS);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(engineStatusReceiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            registerReceiver(engineStatusReceiver, filter);
        }
    }

    @Override
    protected void onStop() {
        unregisterReceiver(engineStatusReceiver);
        super.onStop();
    }

    private void handleIntent(Intent intent) {
        if (intent != null && ACTION_REQUEST_SCREEN_CAPTURE.equals(intent.getAction())) {
            requestScreenCapture();
        }
    }

    private ScrollView createContentView() {
        ScrollView scrollView = new ScrollView(this);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(ROOT_PADDING, ROOT_PADDING, ROOT_PADDING, ROOT_PADDING);
        root.setLayoutParams(matchWidthWrapContent());
        scrollView.addView(root);

        TextView titleView = createTitle("AutoLuaEngine");
        root.addView(titleView, matchWidthWrapContent());

        TextView connectionView = createSmallText(
                "调试端口：127.0.0.1:" + EngineSettings.getHttpPort(this)
        );
        root.addView(connectionView, matchWidthWrapContent());

        addControlSection(root);
        addRunModeSection(root);
        addScriptListSection(root);
        addStatusSection(root);

        return scrollView;
    }

    private void addScriptListSection(LinearLayout root) {
        root.addView(createSectionTitle("我的脚本"), sectionLayoutParams());

        selectedScriptView = createSmallText("");
        updateSelectedScriptView();
        root.addView(selectedScriptView, matchWidthWrapContent());

        ScriptCatalog.ScriptItem[] scripts = ScriptCatalog.builtInScripts();
        for (int i = 0; i < scripts.length; i++) {
            ScriptCatalog.ScriptItem item = scripts[i];
            Button button = createButton(resolveButtonId(item.assetPath), item.title, () -> selectScriptAndRun(item));
            addButton(root, button, i > 0);

            TextView descriptionView = createSmallText(item.description + "    " + item.assetPath);
            root.addView(descriptionView, matchWidthWrapContent());
        }
    }

    private void addRunModeSection(LinearLayout root) {
        root.addView(createSectionTitle("运行模式"), sectionLayoutParams());

        touchButton = createButton(
                R.id.button_run_touch,
                "测试触控和无障碍",
                () -> runScript("scripts/touch.lua")
        );
        addButton(root, touchButton, false);

        captureButton = createButton(
                R.id.button_request_capture,
                "开启截图授权",
                this::requestScreenCapture
        );
        addButton(root, captureButton, true);

        screenButton = createButton(
                R.id.button_run_screen,
                "测试截图和取色",
                () -> runScript("scripts/screen.lua")
        );
        addButton(root, screenButton, true);
    }

    private void addControlSection(LinearLayout root) {
        root.addView(createSectionTitle("运行控制"), sectionLayoutParams());

        Button runSelectedButton = createButton(
                R.id.button_run_lua,
                "运行选中脚本",
                () -> runScript(selectedScript.assetPath)
        );
        runButton = runSelectedButton;
        addButton(root, runSelectedButton, false);

        errorButton = createButton(
                R.id.button_run_error,
                "错误验证",
                () -> runScript("scripts/error.lua")
        );
        addButton(root, errorButton, true);

        loopButton = createButton(
                R.id.button_run_loop,
                "循环停止验证",
                () -> runScript("scripts/loop.lua")
        );
        addButton(root, loopButton, true);

        stopButton = createButton(
                R.id.button_stop,
                "停止脚本",
                () -> EngineService.stopScript(this)
        );
        stopButton.setEnabled(false);
        addButton(root, stopButton, true);

        floatingButton = createButton(
                R.id.button_start_floating,
                "开启悬浮控制",
                this::startFloatingControl
        );
        addButton(root, floatingButton, true);
    }

    private void addStatusSection(LinearLayout root) {
        root.addView(createSectionTitle("状态"), sectionLayoutParams());

        outputView = new TextView(this);
        outputView.setId(R.id.text_output);
        outputView.setText("就绪");
        outputView.setTextSize(16);
        outputView.setGravity(Gravity.START);
        root.addView(outputView, matchWidthWrapContent());
    }

    private TextView createTitle(String text) {
        TextView titleView = new TextView(this);
        titleView.setText(text);
        titleView.setTextSize(24);
        titleView.setGravity(Gravity.START);
        return titleView;
    }

    private TextView createSectionTitle(String text) {
        TextView titleView = new TextView(this);
        titleView.setText(text);
        titleView.setTextSize(18);
        titleView.setGravity(Gravity.START);
        return titleView;
    }

    private TextView createSmallText(String text) {
        TextView textView = new TextView(this);
        textView.setText(text);
        textView.setTextSize(13);
        textView.setGravity(Gravity.START);
        return textView;
    }

    private Button createButton(int id, String text, Runnable action) {
        Button button = new Button(this);
        button.setId(id);
        button.setText(text);
        button.setAllCaps(false);
        button.setOnClickListener(view -> action.run());
        return button;
    }

    private void addButton(LinearLayout root, Button button, boolean withTopMargin) {
        LinearLayout.LayoutParams params = matchWidthWrapContent();
        if (withTopMargin) {
            params.topMargin = ITEM_MARGIN;
        }
        root.addView(button, params);
    }

    private LinearLayout.LayoutParams matchWidthWrapContent() {
        return new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
        );
    }

    private LinearLayout.LayoutParams sectionLayoutParams() {
        LinearLayout.LayoutParams params = matchWidthWrapContent();
        params.topMargin = SECTION_MARGIN;
        return params;
    }

    private int resolveButtonId(String assetPath) {
        if ("scripts/main.lua".equals(assetPath)) {
            return R.id.button_script_main;
        }
        if ("scripts/error.lua".equals(assetPath)) {
            return R.id.button_script_error;
        }
        if ("scripts/loop.lua".equals(assetPath)) {
            return R.id.button_script_loop;
        }
        if ("scripts/touch.lua".equals(assetPath)) {
            return R.id.button_script_touch;
        }
        if ("scripts/screen.lua".equals(assetPath)) {
            return R.id.button_script_screen;
        }
        return R.id.button_script_main;
    }

    private void selectScript(ScriptCatalog.ScriptItem item) {
        selectedScript = item;
        getSharedPreferences(ScriptCatalog.PREF_NAME, MODE_PRIVATE)
                .edit()
                .putString(ScriptCatalog.KEY_SELECTED_SCRIPT_PATH, item.assetPath)
                .apply();
        updateSelectedScriptView();
        outputView.setText("已选择脚本：" + item.title);
    }

    private void selectScriptAndRun(ScriptCatalog.ScriptItem item) {
        selectScript(item);
        runScript(item.assetPath);
    }

    private ScriptCatalog.ScriptItem loadSelectedScript() {
        String assetPath = getSharedPreferences(ScriptCatalog.PREF_NAME, MODE_PRIVATE)
                .getString(ScriptCatalog.KEY_SELECTED_SCRIPT_PATH, ScriptCatalog.DEFAULT_SCRIPT_PATH);
        return ScriptCatalog.findByPath(assetPath);
    }

    private void updateSelectedScriptView() {
        if (selectedScriptView == null || selectedScript == null) {
            return;
        }
        selectedScriptView.setText("当前选择：" + selectedScript.title + "    " + selectedScript.assetPath);
    }

    private void runScript(String assetPath) {
        EngineService.runAssetScript(this, assetPath);
        outputView.setText("准备运行脚本...");
    }

    private void ensureAppFilesDir() {
        getFilesDir();
    }

    private void setRunningState(boolean running) {
        if (runButton != null) {
            runButton.setEnabled(!running);
        }
        if (errorButton != null) {
            errorButton.setEnabled(!running);
        }
        if (loopButton != null) {
            loopButton.setEnabled(!running);
        }
        if (touchButton != null) {
            touchButton.setEnabled(!running);
        }
        if (captureButton != null) {
            captureButton.setEnabled(!running);
        }
        if (screenButton != null) {
            screenButton.setEnabled(!running);
        }
        if (floatingButton != null) {
            floatingButton.setEnabled(!running);
        }
        if (stopButton != null) {
            stopButton.setEnabled(running);
        }
        if (running) {
            outputView.setText("脚本运行中...");
        }
    }

    private void handleEngineStatus(Intent intent) {
        String state = intent.getStringExtra(EngineService.EXTRA_STATE);
        String message = intent.getStringExtra(EngineService.EXTRA_MESSAGE);
        if (EngineService.STATE_RUNNING.equals(state)) {
            setRunningState(true);
        } else if (EngineService.STATE_STOPPING.equals(state)) {
            outputView.setText(message);
        } else {
            setRunningState(false);
        }

        if (message != null && !message.isEmpty()) {
            outputView.setText(message);
        }
    }

    private void requestScreenCapture() {
        Intent intent = ScreenCaptureBridge.createCaptureIntent(this);
        if (intent == null) {
            outputView.setText("当前设备不支持截图授权");
            return;
        }
        startActivityForResult(intent, REQUEST_SCREEN_CAPTURE);
    }

    private void startFloatingControl() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M
                && !Settings.canDrawOverlays(this)) {
            Intent intent = new Intent(
                    Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                    Uri.parse("package:" + getPackageName())
            );
            startActivityForResult(intent, REQUEST_OVERLAY_PERMISSION);
            outputView.setText("请开启悬浮窗权限后再启动悬浮控制");
            return;
        }

        Intent serviceIntent = new Intent(this, FloatingControlService.class);
        startService(serviceIntent);
        outputView.setText("悬浮控制已启动");
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_SCREEN_CAPTURE) {
            if (resultCode == RESULT_OK && data != null) {
                ScreenCaptureBridge.savePermission(resultCode, data);
                outputView.setText("截图授权已开启");
            } else {
                outputView.setText("截图授权已取消");
            }
            return;
        }

        if (requestCode == REQUEST_OVERLAY_PERMISSION) {
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M
                    || Settings.canDrawOverlays(this)) {
                startFloatingControl();
            } else {
                outputView.setText("悬浮窗权限未开启");
            }
        }
    }
}
