package com.autolua.engine;

import android.app.Service;
import android.content.Intent;
import android.graphics.PixelFormat;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.provider.Settings;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.Toast;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

/**
 * 最小悬浮控制入口。
 *
 * 当前只负责常用控制：运行选中脚本、停止、截图授权入口和关闭悬浮窗。
 * 脚本仍通过 NativeEngine 单任务运行；真正的后台 EngineService 和暂停能力后续再拆。
 */
public final class FloatingControlService extends Service {
    private final Handler mainHandler = new Handler(Looper.getMainLooper());

    private WindowManager windowManager;
    private View floatingView;
    private LinearLayout panelView;
    private WindowManager.LayoutParams layoutParams;
    private boolean panelVisible;
    private boolean scriptRunning;
    private int lastTouchX;
    private int lastTouchY;
    private int lastWindowX;
    private int lastWindowY;

    @Override
    public void onCreate() {
        super.onCreate();
        NativeEngine.init(getApplicationContext());
        EngineHttpServer.start(getApplicationContext());
        windowManager = (WindowManager) getSystemService(WINDOW_SERVICE);
        createFloatingViewIfAllowed();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        createFloatingViewIfAllowed();
        return START_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onDestroy() {
        removeFloatingView();
        super.onDestroy();
    }

    private void createFloatingViewIfAllowed() {
        if (floatingView != null) {
            return;
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M
                && !Settings.canDrawOverlays(this)) {
            showToast("请先开启悬浮窗权限");
            stopSelf();
            return;
        }

        floatingView = createFloatingView();
        layoutParams = createLayoutParams();
        windowManager.addView(floatingView, layoutParams);
    }

    private View createFloatingView() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.END);

        Button handleButton = createButton("引擎");
        handleButton.setOnClickListener(view -> togglePanel());
        handleButton.setOnTouchListener(this::handleDragTouch);
        root.addView(handleButton);

        panelView = new LinearLayout(this);
        panelView.setOrientation(LinearLayout.VERTICAL);
        panelView.setVisibility(View.GONE);

        Button runButton = createButton("运行");
        runButton.setOnClickListener(view -> runSelectedScript());
        panelView.addView(runButton);

        Button stopButton = createButton("停止");
        stopButton.setOnClickListener(view -> {
            NativeEngine.stop();
            showToast("已请求停止脚本");
        });
        panelView.addView(stopButton);

        Button pauseButton = createButton("暂停");
        pauseButton.setOnClickListener(view -> showToast("暂停功能后续实现"));
        panelView.addView(pauseButton);

        Button captureButton = createButton("截图");
        captureButton.setOnClickListener(view -> openScreenCapturePermission());
        panelView.addView(captureButton);

        Button closeButton = createButton("关闭");
        closeButton.setOnClickListener(view -> stopSelf());
        panelView.addView(closeButton);

        root.addView(panelView);
        return root;
    }

    private Button createButton(String text) {
        Button button = new Button(this);
        button.setText(text);
        button.setAllCaps(false);
        button.setMinWidth(120);
        button.setMinHeight(72);
        return button;
    }

    private WindowManager.LayoutParams createLayoutParams() {
        int type = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                : WindowManager.LayoutParams.TYPE_PHONE;

        WindowManager.LayoutParams params = new WindowManager.LayoutParams(
                WindowManager.LayoutParams.WRAP_CONTENT,
                WindowManager.LayoutParams.WRAP_CONTENT,
                type,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE,
                PixelFormat.TRANSLUCENT
        );
        params.gravity = Gravity.TOP | Gravity.END;
        params.x = 24;
        params.y = 240;
        return params;
    }

    private boolean handleDragTouch(View view, MotionEvent event) {
        switch (event.getAction()) {
            case MotionEvent.ACTION_DOWN:
                lastTouchX = (int) event.getRawX();
                lastTouchY = (int) event.getRawY();
                lastWindowX = layoutParams.x;
                lastWindowY = layoutParams.y;
                return false;
            case MotionEvent.ACTION_MOVE:
                layoutParams.x = lastWindowX - ((int) event.getRawX() - lastTouchX);
                layoutParams.y = lastWindowY + ((int) event.getRawY() - lastTouchY);
                windowManager.updateViewLayout(floatingView, layoutParams);
                return true;
            default:
                return false;
        }
    }

    private void togglePanel() {
        panelVisible = !panelVisible;
        panelView.setVisibility(panelVisible ? View.VISIBLE : View.GONE);
    }

    private void runSelectedScript() {
        if (scriptRunning) {
            showToast("脚本正在运行");
            return;
        }

        String assetPath = getSharedPreferences(ScriptCatalog.PREF_NAME, MODE_PRIVATE)
                .getString(ScriptCatalog.KEY_SELECTED_SCRIPT_PATH, ScriptCatalog.DEFAULT_SCRIPT_PATH);
        ScriptCatalog.ScriptItem item = ScriptCatalog.findByPath(assetPath);
        scriptRunning = true;
        showToast("开始运行：" + item.title);

        Thread worker = new Thread(() -> {
            String result;
            try {
                result = NativeEngine.runLuaText(readAssetText(assetPath));
            } catch (IOException exception) {
                result = "读取脚本失败：" + exception.getMessage();
            }

            String finalResult = result;
            mainHandler.post(() -> {
                scriptRunning = false;
                showToast(finalResult);
            });
        }, "FloatingLuaWorker");
        worker.start();
    }

    private void openScreenCapturePermission() {
        Intent intent = new Intent(this, MainActivity.class);
        intent.setAction(MainActivity.ACTION_REQUEST_SCREEN_CAPTURE);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        startActivity(intent);
    }

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

    private void removeFloatingView() {
        if (floatingView == null || windowManager == null) {
            return;
        }

        windowManager.removeView(floatingView);
        floatingView = null;
        panelView = null;
    }

    private void showToast(String message) {
        mainHandler.post(() -> Toast.makeText(this, message, Toast.LENGTH_SHORT).show());
    }
}
