package com.autolua.engine;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

/**
 * Android 引擎调试页。
 *
 * 这个页面只保留本机调试入口。真正的脚本编辑、日志面板和设备控制优先
 * 放到 PC/IDE 侧，通过 EngineHttpServer 连接引擎。
 */
public final class MainActivity extends Activity {
    private static final int REQUEST_SCREEN_CAPTURE = 1001;
    private static final int ROOT_PADDING = 48;
    private static final int SECTION_MARGIN = 24;
    private static final int ITEM_MARGIN = 12;

    private TextView outputView;
    private Button runButton;
    private Button errorButton;
    private Button loopButton;
    private Button touchButton;
    private Button captureButton;
    private Button screenButton;
    private Button stopButton;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        ensureAppFilesDir();
        ScreenCaptureBridge.init(getApplicationContext());
        NativeEngine.init(getApplicationContext());
        EngineHttpServer.start(getApplicationContext());
        setContentView(createContentView());
    }

    private LinearLayout createContentView() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER_VERTICAL);
        root.setPadding(ROOT_PADDING, ROOT_PADDING, ROOT_PADDING, ROOT_PADDING);
        root.setLayoutParams(new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
        ));

        TextView titleView = createSectionTitle("AutoLuaEngine Debug");
        root.addView(titleView, matchWidthWrapContent());

        TextView connectionView = createSectionTitle(
                "HTTP JSON-RPC: 127.0.0.1:" + EngineSettings.getHttpPort(this)
        );
        root.addView(connectionView, matchWidthWrapContent());

        addScriptSection(root);
        addPlatformSection(root);
        addControlSection(root);
        addOutputSection(root);

        return root;
    }

    private void addScriptSection(LinearLayout root) {
        root.addView(createSectionTitle("Script Tests"), sectionLayoutParams());

        runButton = createButton(
                R.id.button_run_lua,
                "Run Lua Test",
                () -> runScript("scripts/main.lua")
        );
        addButton(root, runButton, false);

        errorButton = createButton(
                R.id.button_run_error,
                "Run Error Test",
                () -> runScript("scripts/error.lua")
        );
        addButton(root, errorButton, true);

        loopButton = createButton(
                R.id.button_run_loop,
                "Run Loop Test",
                () -> runScript("scripts/loop.lua")
        );
        addButton(root, loopButton, true);
    }

    private void addPlatformSection(LinearLayout root) {
        root.addView(createSectionTitle("Platform Tests"), sectionLayoutParams());

        touchButton = createButton(
                R.id.button_run_touch,
                "Run Touch Test",
                () -> runScript("scripts/touch.lua")
        );
        addButton(root, touchButton, false);

        captureButton = createButton(
                R.id.button_request_capture,
                "Request Screen Capture",
                this::requestScreenCapture
        );
        addButton(root, captureButton, true);

        screenButton = createButton(
                R.id.button_run_screen,
                "Run Screen Test",
                () -> runScript("scripts/screen.lua")
        );
        addButton(root, screenButton, true);
    }

    private void addControlSection(LinearLayout root) {
        root.addView(createSectionTitle("Control"), sectionLayoutParams());

        stopButton = createButton(R.id.button_stop, "Stop", NativeEngine::stop);
        stopButton.setEnabled(false);
        addButton(root, stopButton, false);
    }

    private void addOutputSection(LinearLayout root) {
        root.addView(createSectionTitle("Status"), sectionLayoutParams());

        outputView = new TextView(this);
        outputView.setId(R.id.text_output);
        outputView.setText("Ready");
        outputView.setTextSize(16);
        outputView.setGravity(Gravity.CENTER);
        root.addView(outputView, matchWidthWrapContent());
    }

    private TextView createSectionTitle(String text) {
        TextView titleView = new TextView(this);
        titleView.setText(text);
        titleView.setTextSize(14);
        titleView.setGravity(Gravity.START);
        return titleView;
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

    private void runScript(String assetPath) {
        try {
            String script = readAssetText(assetPath);
            setRunningState(true);

            // 第一版先在 Java 层开后台线程，避免 Lua 的 sleep 阻塞 Android UI。
            // 后续 native ScriptTask 支持异步和 stop 后，会把线程管理下沉到 Engine。
            Thread worker = new Thread(() -> {
                String message = NativeEngine.runLuaText(script);
                runOnUiThread(() -> {
                    outputView.setText(message);
                    setRunningState(false);
                });
            }, "LuaScriptWorker");
            worker.start();
        } catch (IOException exception) {
            outputView.setText("Read script failed: " + exception.getMessage());
        }
    }

    /**
     * 从 assets 读取 UTF-8 文本脚本。
     *
     * 第一版先用 assets 固定脚本验证运行时；后续 IDE 接入后，会改为从 PC/IDE
     * 传入脚本文本或脚本文件路径。
     */
    private String readAssetText(String assetPath) throws IOException {
        try (InputStream inputStream = getAssets().open(assetPath);
             ByteArrayOutputStream outputStream = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[4096];
            int readCount;
            while ((readCount = inputStream.read(buffer)) != -1) {
                outputStream.write(buffer, 0, readCount);
            }
            return outputStream.toString(StandardCharsets.UTF_8.name());
        }
    }

    /**
     * 确保应用私有文件目录存在。
     *
     * Native 层第一版的 file.appDataPath 会返回该目录下的文件路径，
     * 所以 Activity 启动时先创建一次，避免脚本写文件时目录不存在。
     */
    private void ensureAppFilesDir() {
        getFilesDir();
    }

    private void setRunningState(boolean running) {
        runButton.setEnabled(!running);
        errorButton.setEnabled(!running);
        loopButton.setEnabled(!running);
        touchButton.setEnabled(!running);
        captureButton.setEnabled(!running);
        screenButton.setEnabled(!running);
        stopButton.setEnabled(running);
        if (running) {
            outputView.setText("Running...");
        }
    }

    private void requestScreenCapture() {
        Intent intent = ScreenCaptureBridge.createCaptureIntent(this);
        if (intent == null) {
            outputView.setText("Screen capture is not available");
            return;
        }
        startActivityForResult(intent, REQUEST_SCREEN_CAPTURE);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_SCREEN_CAPTURE) {
            return;
        }

        if (resultCode == RESULT_OK && data != null) {
            ScreenCaptureBridge.savePermission(resultCode, data);
            outputView.setText("Screen capture permission granted");
        } else {
            outputView.setText("Screen capture permission denied");
        }
    }
}
