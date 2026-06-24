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
            if (message != null && !message.isEmpty()) {
                showToast(message);
            }
        }
    };

    @Override
    public void onCreate() {
        super.onCreate();
        NativeEngine.init(getApplicationContext());
        EngineHttpServer.start(getApplicationContext());
        windowManager = (WindowManager) getSystemService(WINDOW_SERVICE);
        bubbleSizePx = dp(54);
        touchSlopPx = ViewConfiguration.get(this).getScaledTouchSlop();
        registerEngineStatusReceiver();
        createBubbleViewIfAllowed();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        createBubbleViewIfAllowed();
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

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M
                && !Settings.canDrawOverlays(this)) {
            showToast("请先开启悬浮窗权限");
            stopSelf();
            return;
        }

        bubbleView = createBubbleView();
        bubbleLayoutParams = createBubbleLayoutParams();
        windowManager.addView(bubbleView, bubbleLayoutParams);
    }

    private View createBubbleView() {
        TextView bubble = new TextView(this);
        bubble.setText("引擎");
        bubble.setTextColor(Color.WHITE);
        bubble.setTextSize(13);
        bubble.setGravity(Gravity.CENTER);
        bubble.setBackground(makeOvalDrawable(Color.parseColor("#159FE6"), Color.WHITE, 2));
        bubble.setElevation(dp(8));
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
        params.x = Math.max(0, getResources().getDisplayMetrics().widthPixels - bubbleSizePx);
        params.y = dp(260);
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
        firstRow.addView(createPanelAction("▶", "运行", this::runSelectedScript));
        firstRow.addView(createPanelAction("Ⅱ", "暂停", () -> showToast("暂停功能后续实现")));
        firstRow.addView(createPanelAction("■", "停止", () -> EngineService.stopScript(this)));
        panel.addView(firstRow, matchWidthWrapContent());

        LinearLayout secondRow = createActionRow();
        secondRow.addView(createPanelAction("照", "截图", this::openScreenCapturePermission));
        secondRow.addView(createPanelAction("关", "关闭服务", this::stopSelf));
        secondRow.addView(createPanelAction("隐", "隐藏", this::removePanelView));
        LinearLayout.LayoutParams secondRowParams = matchWidthWrapContent();
        secondRowParams.topMargin = dp(22);
        panel.addView(secondRow, secondRowParams);

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

    private void runSelectedScript() {
        String assetPath = getSharedPreferences(ScriptCatalog.PREF_NAME, MODE_PRIVATE)
                .getString(ScriptCatalog.KEY_SELECTED_SCRIPT_PATH, ScriptCatalog.DEFAULT_SCRIPT_PATH);
        ScriptCatalog.ScriptItem item = ScriptCatalog.findByPath(assetPath);
        EngineService.runAssetScript(this, assetPath);
        showToast("已发送运行命令：" + item.title);
    }

    private void openScreenCapturePermission() {
        Intent intent = new Intent(this, MainActivity.class);
        intent.setAction(MainActivity.ACTION_REQUEST_SCREEN_CAPTURE);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        startActivity(intent);
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
    }

    private void showToast(String message) {
        mainHandler.post(() -> Toast.makeText(this, message, Toast.LENGTH_SHORT).show());
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

    private int dp(int value) {
        return (int) (value * getResources().getDisplayMetrics().density + 0.5f);
    }
}
