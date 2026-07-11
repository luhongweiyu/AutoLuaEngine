/**
 * 文件用途：主进程悬浮按钮服务，提供脚本运行/停止、隐藏和强制停止进程入口。
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
    private View panelView;
    private WindowManager.LayoutParams bubbleLayoutParams;
    private WindowManager.LayoutParams panelLayoutParams;
    private int bubbleSizePx;
    private boolean scriptRunning;
    private String scriptState = EngineService.STATE_FINISHED;
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
        if (panelView == null) {
            showPanelView();
        } else {
            removePanelView();
        }
    }

    private void showPanelView() {
        if (panelView != null) {
            return;
        }

        panelView = createPanelView();
        panelLayoutParams = createPanelLayoutParams();
        windowManager.addView(panelView, panelLayoutParams);
        panelView.post(this::updatePanelPosition);
        EngineSettings.setFloatingPanelExpanded(this, true);
    }

    private WindowManager.LayoutParams createPanelLayoutParams() {
        int type = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                : WindowManager.LayoutParams.TYPE_PHONE;

        WindowManager.LayoutParams params = new WindowManager.LayoutParams(
                getPanelWidthPx(),
                WindowManager.LayoutParams.WRAP_CONTENT,
                type,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE,
                PixelFormat.TRANSLUCENT
        );
        params.gravity = Gravity.TOP | Gravity.START;
        updatePanelPosition(params, dp(96));
        return params;
    }

    private View createPanelView() {
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(dp(12), dp(12), dp(12), dp(10));
        panel.setClickable(true);
        panel.setBackground(makeRoundDrawable(Color.parseColor("#E6333333"), dp(12)));
        panel.setElevation(dp(8));

        LinearLayout firstRow = createActionRow();
        firstRow.addView(createPanelAction(resolveRunPauseIcon(), resolveRunPauseLabel(), this::runPauseOrResume));
        firstRow.addView(createPanelAction("■", "停止", this::stopRunningScriptFromPanel));
        firstRow.addView(createPanelAction("隐", "隐藏", this::hideBubbleAndRemember));
        firstRow.addView(createPanelAction("停", "强停进程", this::forceStopEngineProcess));
        panel.addView(firstRow, matchWidthWrapContent());
        return panel;
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
        icon.setTextSize(22);
        icon.setGravity(Gravity.CENTER);
        icon.setBackground(makeOvalDrawable(Color.WHITE, Color.TRANSPARENT, 0));
        item.addView(icon, new LinearLayout.LayoutParams(dp(44), dp(44)));

        TextView label = new TextView(this);
        label.setText(labelText);
        label.setTextColor(Color.WHITE);
        label.setTextSize(12);
        label.setGravity(Gravity.CENTER);
        label.setSingleLine(true);
        LinearLayout.LayoutParams labelParams = matchWidthWrapContent();
        labelParams.topMargin = dp(5);
        item.addView(label, labelParams);

        return item;
    }

    private String resolveRunPauseIcon() {
        if (EngineService.STATE_RUNNING.equals(scriptState)) {
            return "Ⅱ";
        }
        return "▶";
    }

    private String resolveRunPauseLabel() {
        if (EngineService.STATE_RUNNING.equals(scriptState)) {
            return "暂停";
        }
        if (EngineService.STATE_PAUSING.equals(scriptState)
                || EngineService.STATE_PAUSED.equals(scriptState)) {
            return "继续";
        }
        return "运行";
    }

    private void runPauseOrResume() {
        if (EngineService.STATE_RUNNING.equals(scriptState)) {
            EngineService.pauseScript(this);
            updateRunningState(EngineService.STATE_PAUSING);
            showToast("已请求暂停脚本");
            return;
        }

        if (EngineService.STATE_PAUSING.equals(scriptState)
                || EngineService.STATE_PAUSED.equals(scriptState)) {
            EngineService.resumeScript(this);
            updateRunningState(EngineService.STATE_RUNNING);
            showToast("已请求继续脚本");
            return;
        }

        runSelectedScriptFromPanel();
    }

    private void runSelectedScriptFromPanel() {
        ScriptCatalog.ScriptItem item = ScriptCatalog.getSelectedScript(this);
        if (item == null) {
            showToast("脚本目录为空");
            return;
        }
        if (!item.runnable) {
            showToast("当前只支持运行 Lua 文件：" + item.fileName);
            return;
        }

        EngineService.runScriptFile(this, item.filePath);
        updateRunningState(EngineService.STATE_RUNNING);
        showToast("已发送运行命令：" + item.fileName);
    }

    private void stopRunningScriptFromPanel() {
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
        updatePanelPosition();
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
        if (panelView == null || windowManager == null) {
            return;
        }

        windowManager.removeView(panelView);
        panelView = null;
        panelLayoutParams = null;
        EngineSettings.setFloatingPanelExpanded(this, false);
    }

    private int getPanelWidthPx() {
        return Math.min(dp(320), getResources().getDisplayMetrics().widthPixels - dp(24));
    }

    private void updatePanelPosition() {
        if (panelView == null || panelLayoutParams == null || windowManager == null) {
            return;
        }

        int panelHeight = panelView.getHeight() > 0 ? panelView.getHeight() : dp(96);
        updatePanelPosition(panelLayoutParams, panelHeight);
        windowManager.updateViewLayout(panelView, panelLayoutParams);
    }

    private void updatePanelPosition(WindowManager.LayoutParams params, int panelHeight) {
        if (bubbleLayoutParams == null) {
            return;
        }

        int margin = dp(8);
        int spacing = dp(8);
        int screenWidth = getResources().getDisplayMetrics().widthPixels;
        int screenHeight = getResources().getDisplayMetrics().heightPixels;
        int panelWidth = getPanelWidthPx();

        int bubbleRight = bubbleLayoutParams.x + bubbleSizePx;
        int bubbleBottom = bubbleLayoutParams.y + bubbleSizePx;
        int bubbleCenterX = bubbleLayoutParams.x + bubbleSizePx / 2;
        int bubbleCenterY = bubbleLayoutParams.y + bubbleSizePx / 2;

        int x = bubbleCenterX < screenWidth / 2
                ? bubbleLayoutParams.x
                : bubbleRight - panelWidth;
        int y = bubbleCenterY < screenHeight / 2
                ? bubbleBottom + spacing
                : bubbleLayoutParams.y - spacing - panelHeight;

        params.x = Math.max(margin, Math.min(x, screenWidth - panelWidth - margin));
        params.y = Math.max(margin, Math.min(y, screenHeight - panelHeight - margin));
    }

    private void showToast(String message) {
        mainHandler.post(() -> Toast.makeText(this, message, Toast.LENGTH_SHORT).show());
    }

    private void updateRunningState(String state) {
        scriptState = state == null || state.isEmpty() ? EngineService.STATE_FINISHED : state;
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
