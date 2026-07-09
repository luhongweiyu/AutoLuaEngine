/**
 * 文件用途：主进程悬浮按钮服务，提供脚本运行、停止、暂停、继续和快捷入口。
 */
package com.autolua.engine;

import android.app.Service;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.graphics.PixelFormat;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.provider.Settings;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import org.json.JSONObject;

/**
 * 系统级悬浮控制入口。
 *
 * 这里使用 TYPE_APPLICATION_OVERLAY 创建贴边小圆点，用户回到 Home 或切换到其他 App 后
 * 仍然可以点开控制面板。脚本运行统一交给 EngineService，本服务只负责浮窗交互。
 */
public final class FloatingControlService extends Service {
    private final Handler mainHandler = new Handler(Looper.getMainLooper());

    private WindowManager windowManager;
    private View bubbleView;
    private View panelOverlayView;
    private WindowManager.LayoutParams bubbleLayoutParams;
    private int bubbleSizePx;
    private boolean scriptRunning;
    private int touchSlopPx;
    private int lastTouchX;
    private int lastTouchY;
    private int lastWindowX;
    private int lastWindowY;
    private boolean draggingBubble;
    private final BroadcastReceiver engineStatusReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            String message = intent.getStringExtra(EngineService.EXTRA_MESSAGE);
            updateRunningState(intent.getStringExtra(EngineService.EXTRA_STATE));
            if (message != null && !message.isEmpty()) {
                showToast(message);
            }
        }
    };

    @Override
    public void onCreate() {
        super.onCreate();
        EngineService.ensureStarted(this);
        windowManager = (WindowManager) getSystemService(WINDOW_SERVICE);
        bubbleSizePx = dp(28);
        touchSlopPx = ViewConfiguration.get(this).getScaledTouchSlop();
        registerEngineStatusReceiver();
        createBubbleViewIfAllowed();
        refreshRunningStateFromEngine();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        createBubbleViewIfAllowed();
        refreshRunningStateFromEngine();
        return START_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onDestroy() {
        unregisterReceiver(engineStatusReceiver);
        removePanelView();
        removeBubbleView();
        super.onDestroy();
    }

    private void createBubbleViewIfAllowed() {
        if (bubbleView != null) {
            return;
        }

        if (EngineSettings.isFloatingBubbleHidden(this)) {
            return;
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M
                && !Settings.canDrawOverlays(this)) {
            showToast("请先开启悬浮窗权限");
            stopSelf();
            return;
        }

        bubbleView = createBubbleView();
        bubbleLayoutParams = createBubbleLayoutParams();
        windowManager.addView(bubbleView, bubbleLayoutParams);

        if (EngineSettings.isFloatingPanelExpanded(this)) {
            showPanelView();
        }
    }

    private View createBubbleView() {
        TextView bubble = new TextView(this);
        bubble.setText("");
        bubble.setContentDescription("AutoLuaEngine 悬浮按钮");
        bubble.setTextColor(Color.WHITE);
        bubble.setGravity(Gravity.CENTER);
        bubble.setBackground(makeBubbleDrawable());
        bubble.setElevation(dp(6));
        bubble.setOnTouchListener(this::handleBubbleTouch);
        return bubble;
    }

    private WindowManager.LayoutParams createBubbleLayoutParams() {
        int type = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                : WindowManager.LayoutParams.TYPE_PHONE;

        WindowManager.LayoutParams params = new WindowManager.LayoutParams(
                bubbleSizePx,
                bubbleSizePx,
                type,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE,
                PixelFormat.TRANSLUCENT
        );
        params.gravity = Gravity.TOP | Gravity.START;
        int defaultX = Math.max(0, getResources().getDisplayMetrics().widthPixels - bubbleSizePx);
        int defaultY = dp(260);
        params.x = EngineSettings.getFloatingBubbleX(this, defaultX);
        params.y = EngineSettings.getFloatingBubbleY(this, defaultY);
        clampBubbleLayoutParams(params);
        return params;
    }

    private boolean handleBubbleTouch(View view, MotionEvent event) {
        switch (event.getAction()) {
            case MotionEvent.ACTION_DOWN:
                lastTouchX = (int) event.getRawX();
                lastTouchY = (int) event.getRawY();
                lastWindowX = bubbleLayoutParams.x;
                lastWindowY = bubbleLayoutParams.y;
                draggingBubble = false;
                return true;
            case MotionEvent.ACTION_MOVE:
                int offsetX = (int) event.getRawX() - lastTouchX;
                int offsetY = (int) event.getRawY() - lastTouchY;
                draggingBubble = draggingBubble
                        || Math.abs(offsetX) > touchSlopPx
                        || Math.abs(offsetY) > touchSlopPx;
                moveBubbleTo(lastWindowX + offsetX, lastWindowY + offsetY);
                return true;
            case MotionEvent.ACTION_UP:
                if (draggingBubble) {
                    snapBubbleToScreenEdge();
                } else {
                    togglePanel();
                }
                return true;
            default:
                return true;
        }
    }

    private void togglePanel() {
        if (panelOverlayView == null) {
            showPanelView();
        } else {
            removePanelView();
        }
    }

    private void showPanelView() {
        if (panelOverlayView != null) {
            return;
        }

        panelOverlayView = createPanelOverlayView();
        windowManager.addView(panelOverlayView, createPanelLayoutParams());
        EngineSettings.setFloatingPanelExpanded(this, true);
    }

    private WindowManager.LayoutParams createPanelLayoutParams() {
        int type = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                : WindowManager.LayoutParams.TYPE_PHONE;

        WindowManager.LayoutParams params = new WindowManager.LayoutParams(
                WindowManager.LayoutParams.MATCH_PARENT,
                WindowManager.LayoutParams.MATCH_PARENT,
                type,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE,
                PixelFormat.TRANSLUCENT
        );
        params.gravity = Gravity.TOP | Gravity.START;
        return params;
    }

    private View createPanelOverlayView() {
        FrameLayout overlay = new FrameLayout(this);
        overlay.setBackgroundColor(Color.parseColor("#66000000"));
        overlay.setClickable(true);
        overlay.setOnClickListener(view -> removePanelView());

        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(dp(22), dp(18), dp(22), dp(18));
        panel.setClickable(true);
        panel.setBackground(makeRoundDrawable(Color.parseColor("#E6333333"), dp(14)));

        TextView status = new TextView(this);
        status.setText("服务状态  开启");
        status.setTextColor(Color.WHITE);
        status.setTextSize(18);
        status.setGravity(Gravity.START);
        panel.addView(status, matchWidthWrapContent());

        View divider = new View(this);
        divider.setBackgroundColor(Color.parseColor("#66FFFFFF"));
        LinearLayout.LayoutParams dividerParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                dp(1)
        );
        dividerParams.setMargins(0, dp(14), 0, dp(18));
        panel.addView(divider, dividerParams);

        LinearLayout firstRow = createActionRow();
        firstRow.addView(createPanelAction(
                scriptRunning ? "■" : "▶",
                scriptRunning ? "停止" : "运行",
                this::toggleScriptRunning
        ));
        firstRow.addView(createPanelAction("Ⅱ", "暂停", () -> EngineService.pauseScript(this)));
        firstRow.addView(createPanelAction("续", "继续", () -> EngineService.resumeScript(this)));
        firstRow.addView(createPanelAction("■", "停止", () -> EngineService.stopScript(this)));
        panel.addView(firstRow, matchWidthWrapContent());

        LinearLayout secondRow = createActionRow();
        secondRow.addView(createPanelAction("照", "截图", this::openScreenCapturePermission));
        secondRow.addView(createPanelAction("志", "日志", this::openLogPanel));
        secondRow.addView(createPanelAction("设", "设置", this::openSettingsPanel));
        LinearLayout.LayoutParams secondRowParams = matchWidthWrapContent();
        secondRowParams.topMargin = dp(22);
        panel.addView(secondRow, secondRowParams);

        LinearLayout thirdRow = createActionRow();
        thirdRow.addView(createPanelAction("事", "事件", this::showUserEventPlaceholder));
        thirdRow.addView(createPanelAction("隐", "隐藏", this::hideBubbleAndRemember));
        thirdRow.addView(createPanelAction("停", "强停进程", this::forceStopEngineProcess));
        LinearLayout.LayoutParams thirdRowParams = matchWidthWrapContent();
        thirdRowParams.topMargin = dp(22);
        panel.addView(thirdRow, thirdRowParams);

        FrameLayout.LayoutParams panelParams = new FrameLayout.LayoutParams(
                Math.min(dp(360), getResources().getDisplayMetrics().widthPixels - dp(32)),
                FrameLayout.LayoutParams.WRAP_CONTENT,
                Gravity.CENTER
        );
        overlay.addView(panel, panelParams);
        return overlay;
    }

    private LinearLayout createActionRow() {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER);
        return row;
    }

    private View createPanelAction(String iconText, String labelText, Runnable action) {
        LinearLayout item = new LinearLayout(this);
        item.setOrientation(LinearLayout.VERTICAL);
        item.setGravity(Gravity.CENTER);
        item.setClickable(true);
        item.setLayoutParams(new LinearLayout.LayoutParams(
                0,
                LinearLayout.LayoutParams.WRAP_CONTENT,
                1f
        ));
        item.setOnClickListener(view -> {
            removePanelView();
            action.run();
        });

        TextView icon = new TextView(this);
        icon.setText(iconText);
        icon.setTextColor(Color.parseColor("#159FE6"));
        icon.setTextSize(28);
        icon.setGravity(Gravity.CENTER);
        icon.setBackground(makeOvalDrawable(Color.WHITE, Color.TRANSPARENT, 0));
        item.addView(icon, new LinearLayout.LayoutParams(dp(68), dp(68)));

        TextView label = new TextView(this);
        label.setText(labelText);
        label.setTextColor(Color.WHITE);
        label.setTextSize(15);
        label.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams labelParams = matchWidthWrapContent();
        labelParams.topMargin = dp(8);
        item.addView(label, labelParams);

        return item;
    }

    private void toggleScriptRunning() {
        if (scriptRunning) {
            stopRunningScriptFromRunAction();
            return;
        }

        ScriptCatalog.ScriptItem item = ScriptCatalog.getSelectedScript(this);
        if (item == null) {
            showToast("脚本目录为空");
            return;
        }

        EngineService.runScriptFile(this, item.filePath);
        updateRunningState(EngineService.STATE_RUNNING);
        showToast("已发送运行命令：" + item.fileName);
    }

    private void stopRunningScriptFromRunAction() {
        EngineService.stopScript(this);
        updateRunningState(EngineService.STATE_STOPPING);
        showToast("已请求停止脚本");
    }

    private void forceStopEngineProcess() {
        EngineService.forceStopEngineProcess(this);
        updateRunningState(EngineService.STATE_FINISHED);
        EngineSettings.setFloatingPanelExpanded(this, false);
        removePanelView();
        showToast("已强制停止引擎进程");
    }

    private void openScreenCapturePermission() {
        EngineSettings.setFloatingPanelExpanded(this, false);
        Intent intent = new Intent(this, MainActivity.class);
        intent.setAction(MainActivity.ACTION_REQUEST_SCREEN_CAPTURE);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        startActivity(intent);
    }

    private void openLogPanel() {
        openMainActivity(MainActivity.ACTION_SHOW_LOGS);
    }

    private void openSettingsPanel() {
        openMainActivity(MainActivity.ACTION_SHOW_SETTINGS);
    }

    private void openMainActivity(String action) {
        EngineSettings.setFloatingPanelExpanded(this, false);
        Intent intent = new Intent(this, MainActivity.class);
        intent.setAction(action);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK
                | Intent.FLAG_ACTIVITY_CLEAR_TOP
                | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        startActivity(intent);
    }

    private void showUserEventPlaceholder() {
        showToast("用户事件入口已预留");
    }

    private void hideBubbleAndRemember() {
        EngineSettings.setFloatingBubbleHidden(this, true);
        EngineSettings.setFloatingPanelExpanded(this, false);
        removePanelView();
        removeBubbleView();
        showToast("悬浮按钮已隐藏，可在 App 内重新开启");
    }

    private void moveBubbleTo(int x, int y) {
        if (bubbleView == null || bubbleLayoutParams == null) {
            return;
        }

        int maxX = Math.max(0, getResources().getDisplayMetrics().widthPixels - bubbleSizePx);
        int maxY = Math.max(0, getResources().getDisplayMetrics().heightPixels - bubbleSizePx);
        bubbleLayoutParams.x = Math.max(0, Math.min(x, maxX));
        bubbleLayoutParams.y = Math.max(0, Math.min(y, maxY));
        windowManager.updateViewLayout(bubbleView, bubbleLayoutParams);
    }

    private void snapBubbleToScreenEdge() {
        int screenWidth = getResources().getDisplayMetrics().widthPixels;
        int centerX = bubbleLayoutParams.x + bubbleSizePx / 2;
        int targetX = centerX < screenWidth / 2 ? 0 : screenWidth - bubbleSizePx;
        moveBubbleTo(targetX, bubbleLayoutParams.y);
        EngineSettings.setFloatingBubblePosition(
                this,
                bubbleLayoutParams.x,
                bubbleLayoutParams.y
        );
    }

    private void clampBubbleLayoutParams(WindowManager.LayoutParams params) {
        int maxX = Math.max(0, getResources().getDisplayMetrics().widthPixels - bubbleSizePx);
        int maxY = Math.max(0, getResources().getDisplayMetrics().heightPixels - bubbleSizePx);
        params.x = Math.max(0, Math.min(params.x, maxX));
        params.y = Math.max(0, Math.min(params.y, maxY));
    }

    private void removeBubbleView() {
        if (bubbleView == null || windowManager == null) {
            return;
        }

        windowManager.removeView(bubbleView);
        bubbleView = null;
        bubbleLayoutParams = null;
    }

    private void removePanelView() {
        if (panelOverlayView == null || windowManager == null) {
            return;
        }

        windowManager.removeView(panelOverlayView);
        panelOverlayView = null;
        EngineSettings.setFloatingPanelExpanded(this, false);
    }

    private void showToast(String message) {
        mainHandler.post(() -> Toast.makeText(this, message, Toast.LENGTH_SHORT).show());
    }

    private void updateRunningState(String state) {
        boolean running = EngineService.STATE_RUNNING.equals(state)
                || EngineService.STATE_PAUSING.equals(state)
                || EngineService.STATE_PAUSED.equals(state)
                || EngineService.STATE_STOPPING.equals(state);
        scriptRunning = running;
        mainHandler.post(() -> {
            if (bubbleView != null) {
                bubbleView.setBackground(makeBubbleDrawable());
            }
        });
    }

    private void refreshRunningStateFromEngine() {
        new Thread(() -> {
            try {
                JSONObject params = new JSONObject();
                params.put("taskId", 0);
                JSONObject taskStatus = EngineLocalClient.call(this, "script.status", params);
                updateRunningState(taskStatus.optString("status", ""));
            } catch (Exception ignored) {
                // HTTP 不通代表当前没有可确认的运行任务，悬浮按钮必须恢复未运行颜色。
                updateRunningState(EngineService.STATE_FINISHED);
            }
        }, "FloatingRunningStateQuery").start();
    }

    private void registerEngineStatusReceiver() {
        IntentFilter filter = new IntentFilter(EngineService.ACTION_STATUS);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(engineStatusReceiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            registerReceiver(engineStatusReceiver, filter);
        }
    }

    private LinearLayout.LayoutParams matchWidthWrapContent() {
        return new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
        );
    }

    private GradientDrawable makeOvalDrawable(int color, int strokeColor, int strokeDp) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setShape(GradientDrawable.OVAL);
        drawable.setColor(color);
        if (strokeDp > 0) {
            drawable.setStroke(dp(strokeDp), strokeColor);
        }
        return drawable;
    }

    private GradientDrawable makeRoundDrawable(int color, int radiusPx) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(color);
        drawable.setCornerRadius(radiusPx);
        return drawable;
    }

    private GradientDrawable makeBubbleDrawable() {
        int color = scriptRunning
                ? Color.parseColor("#16A34A")
                : Color.parseColor("#159FE6");
        return makeOvalDrawable(color, Color.WHITE, 1);
    }

    private int dp(int value) {
        return (int) (value * getResources().getDisplayMetrics().density + 0.5f);
    }
}
