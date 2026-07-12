/**
 * 文件用途：在 App 主进程维护脚本 HUD 悬浮层及其交互事件。
 */
package com.autolua.engine;

import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.PixelFormat;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.provider.Settings;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;

/**
 * 脚本 HUD 悬浮服务。
 *
 * HUD 与引擎固定悬浮按钮分开维护：FloatingControlService 只负责引擎控制，当前服务
 * 只负责脚本创建的文本、进度状态和按钮，不会把脚本 UI 逻辑混进 App 固定前端。
 */
public final class ScriptHudService extends Service {
    public static final String ACTION_SHOW = "com.autolua.engine.action.SCRIPT_HUD_SHOW";
    public static final String ACTION_UPDATE = "com.autolua.engine.action.SCRIPT_HUD_UPDATE";
    public static final String ACTION_CLOSE = "com.autolua.engine.action.SCRIPT_HUD_CLOSE";
    public static final String ACTION_CLOSE_ALL = "com.autolua.engine.action.SCRIPT_HUD_CLOSE_ALL";

    private final Handler mainHandler = new Handler();
    private final Map<Long, HudEntry> entries = new HashMap<>();
    private WindowManager windowManager;

    /**
     * 从任意应用进程发出 HUD 命令。Service 默认运行在 App 主进程。
     */
    public static boolean sendCommand(Context context, String action, long sessionId, String specJson) {
        if (context == null) {
            return false;
        }
        if ((ACTION_SHOW.equals(action) || ACTION_UPDATE.equals(action))
                && Build.VERSION.SDK_INT >= Build.VERSION_CODES.M
                && !Settings.canDrawOverlays(context)) {
            return false;
        }

        Intent intent = new Intent(context, ScriptHudService.class);
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
    public void onCreate() {
        super.onCreate();
        windowManager = (WindowManager) getSystemService(WINDOW_SERVICE);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent == null || intent.getAction() == null) {
            return START_NOT_STICKY;
        }

        String action = intent.getAction();
        long sessionId = intent.getLongExtra(ScriptUiProtocol.EXTRA_SESSION_ID, 0);
        if (ACTION_CLOSE_ALL.equals(action)) {
            closeAll(false);
        } else if (ACTION_CLOSE.equals(action)) {
            closeHud(sessionId, false);
        } else if (ACTION_SHOW.equals(action)) {
            showHud(sessionId, readSpec(intent));
        } else if (ACTION_UPDATE.equals(action)) {
            updateHud(sessionId, readSpec(intent));
        }
        return START_NOT_STICKY;
    }

    @Override
    public void onDestroy() {
        closeAll(false);
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    private JSONObject readSpec(Intent intent) {
        try {
            return new JSONObject(intent.getStringExtra(ScriptUiProtocol.EXTRA_SPEC_JSON));
        } catch (Exception exception) {
            return new JSONObject();
        }
    }

    /**
     * 创建或替换一个 HUD 会话。替换时保持同一个 native 会话 ID。
     */
    private void showHud(long sessionId, JSONObject spec) {
        if (sessionId <= 0 || windowManager == null) {
            return;
        }
        closeHud(sessionId, false);

        HudEntry entry = createEntry(sessionId, spec);
        entries.put(sessionId, entry);
        try {
            windowManager.addView(entry.root, entry.layoutParams);
        } catch (RuntimeException exception) {
            entries.remove(sessionId);
            ScriptUiEventDispatcher.dispatch(this, sessionId, "error", makeData(
                    "message",
                    "HUD 创建失败：" + exception.getMessage()
            ));
            return;
        }
        scheduleTimeout(entry);
    }

    /**
     * 合并更新参数后重新构建 HUD。这样按钮、样式、位置和尺寸都能一致更新。
     */
    private void updateHud(long sessionId, JSONObject patch) {
        HudEntry current = entries.get(sessionId);
        if (current == null) {
            ScriptUiEventDispatcher.dispatch(this, sessionId, "error", makeData("message", "HUD 不存在"));
            return;
        }

        JSONObject merged = mergeJson(current.spec, patch);
        closeHud(sessionId, false);
        showHud(sessionId, merged);
    }

    private HudEntry createEntry(long sessionId, JSONObject spec) {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER_VERTICAL);
        int padding = Math.max(0, spec.optInt("padding", dp(8)));
        root.setPadding(padding, padding, padding, padding);
        root.setAlpha(clampAlpha(spec.optDouble("alpha", 1.0)));

        GradientDrawable background = new GradientDrawable();
        background.setColor(readColor(spec, "backgroundColor", Color.parseColor("#CC202124")));
        background.setCornerRadius(Math.max(0, spec.optInt("radius", dp(8))));
        root.setBackground(background);

        TextView textView = new TextView(this);
        textView.setText(spec.optString("text", ""));
        textView.setTextColor(readColor(spec, "textColor", Color.WHITE));
        textView.setTextSize(spec.optInt("textSize", 14));
        textView.setGravity(Gravity.CENTER_VERTICAL);
        root.addView(textView, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
        ));

        JSONArray buttons = spec.optJSONArray("buttons");
        boolean hasButtons = buttons != null && buttons.length() > 0;
        if (hasButtons) {
            LinearLayout buttonRow = new LinearLayout(this);
            buttonRow.setOrientation(LinearLayout.HORIZONTAL);
            buttonRow.setGravity(Gravity.CENTER_VERTICAL);
            LinearLayout.LayoutParams rowParams = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.WRAP_CONTENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT
            );
            rowParams.topMargin = dp(6);
            root.addView(buttonRow, rowParams);
            for (int index = 0; index < buttons.length(); index++) {
                JSONObject buttonSpec = buttons.optJSONObject(index);
                if (buttonSpec != null) {
                    addHudButton(buttonRow, sessionId, buttonSpec, index);
                }
            }
        }

        WindowManager.LayoutParams layoutParams = createLayoutParams(spec, hasButtons);
        HudEntry entry = new HudEntry(sessionId, root, layoutParams, spec);
        installTouchHandler(entry, hasButtons);
        return entry;
    }

    private void addHudButton(
            LinearLayout parent,
            long sessionId,
            JSONObject spec,
            int index
    ) {
        Button button = new Button(this);
        button.setAllCaps(false);
        button.setText(spec.optString("text", "按钮"));
        String buttonId = spec.optString("id", String.valueOf(index + 1));
        button.setOnClickListener(view -> {
            JSONObject data = makeData("id", buttonId);
            try {
                data.put("text", spec.optString("text", "按钮"));
            } catch (JSONException ignored) {
                // 基础字符串写入不会失败。
            }
            ScriptUiEventDispatcher.dispatch(this, sessionId, "click", data);
        });
        parent.addView(button, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
        ));
    }

    private WindowManager.LayoutParams createLayoutParams(JSONObject spec, boolean hasButtons) {
        int type = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                : WindowManager.LayoutParams.TYPE_PHONE;
        boolean touchable = spec.optBoolean(
                "touchable",
                hasButtons || spec.optBoolean("clickable", false) || spec.optBoolean("draggable", false)
        );
        int flags = WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE;
        if (!touchable) {
            flags |= WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE;
        }

        int width = spec.has("width")
                ? spec.optInt("width", WindowManager.LayoutParams.WRAP_CONTENT)
                : WindowManager.LayoutParams.WRAP_CONTENT;
        int height = spec.has("height")
                ? spec.optInt("height", WindowManager.LayoutParams.WRAP_CONTENT)
                : WindowManager.LayoutParams.WRAP_CONTENT;
        WindowManager.LayoutParams params = new WindowManager.LayoutParams(
                width,
                height,
                type,
                flags,
                PixelFormat.TRANSLUCENT
        );

        String gravity = spec.optString("gravity", "top_left");
        if ("center".equalsIgnoreCase(gravity)) {
            params.gravity = Gravity.CENTER;
        } else if ("bottom".equalsIgnoreCase(gravity)) {
            params.gravity = Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL;
        } else {
            params.gravity = Gravity.TOP | Gravity.START;
        }
        params.x = spec.optInt("x", 0);
        params.y = spec.optInt("y", 0);
        return params;
    }

    /**
     * HUD 默认只显示信息；设置 clickable 或 draggable 后才占用触摸事件。
     */
    private void installTouchHandler(HudEntry entry, boolean hasButtons) {
        boolean clickable = entry.spec.optBoolean("clickable", false);
        boolean draggable = entry.spec.optBoolean("draggable", false);
        if (!clickable && !draggable && !hasButtons) {
            return;
        }

        int touchSlop = ViewConfiguration.get(this).getScaledTouchSlop();
        entry.root.setOnTouchListener(new View.OnTouchListener() {
            private int downRawX;
            private int downRawY;
            private int startX;
            private int startY;
            private boolean moved;

            @Override
            public boolean onTouch(View view, MotionEvent event) {
                switch (event.getAction()) {
                    case MotionEvent.ACTION_DOWN:
                        downRawX = (int) event.getRawX();
                        downRawY = (int) event.getRawY();
                        startX = entry.layoutParams.x;
                        startY = entry.layoutParams.y;
                        moved = false;
                        return true;
                    case MotionEvent.ACTION_MOVE:
                        if (!draggable) {
                            return clickable;
                        }
                        int offsetX = (int) event.getRawX() - downRawX;
                        int offsetY = (int) event.getRawY() - downRawY;
                        moved = moved || Math.abs(offsetX) > touchSlop || Math.abs(offsetY) > touchSlop;
                        entry.layoutParams.x = startX + offsetX;
                        entry.layoutParams.y = startY + offsetY;
                        updateHudLayout(entry);
                        return true;
                    case MotionEvent.ACTION_UP:
                        if (!moved && clickable) {
                            ScriptUiEventDispatcher.dispatch(
                                    ScriptHudService.this,
                                    entry.sessionId,
                                    "click",
                                    makeData("id", entry.spec.optString("clickId", "hud"))
                            );
                        }
                        return clickable || draggable;
                    default:
                        return clickable || draggable;
                }
            }
        });
    }

    private void updateHudLayout(HudEntry entry) {
        try {
            if (entry.root.getParent() != null) {
                windowManager.updateViewLayout(entry.root, entry.layoutParams);
            }
        } catch (RuntimeException ignored) {
            // 外部系统移除窗口时，本次拖动更新可直接结束。
        }
    }

    private void scheduleTimeout(HudEntry entry) {
        int durationMs = entry.spec.optInt("durationMs", 0);
        if (durationMs <= 0) {
            return;
        }
        entry.timeoutRunnable = () -> closeHud(entry.sessionId, true);
        mainHandler.postDelayed(entry.timeoutRunnable, durationMs);
    }

    private void closeHud(long sessionId, boolean notifyScript) {
        HudEntry entry = entries.remove(sessionId);
        if (entry == null) {
            return;
        }
        if (entry.timeoutRunnable != null) {
            mainHandler.removeCallbacks(entry.timeoutRunnable);
        }
        try {
            if (entry.root.getParent() != null) {
                windowManager.removeView(entry.root);
            }
        } catch (RuntimeException ignored) {
            // 系统已经移除窗口时无需再次处理。
        }

        if (notifyScript) {
            ScriptUiEventDispatcher.dispatch(this, sessionId, "closed", makeData("reason", "timeout"));
        }
    }

    private void closeAll(boolean notifyScript) {
        Long[] sessionIds = entries.keySet().toArray(new Long[0]);
        for (Long sessionId : sessionIds) {
            closeHud(sessionId, notifyScript);
        }
    }

    private static JSONObject mergeJson(JSONObject source, JSONObject patch) {
        JSONObject result;
        try {
            result = new JSONObject(source.toString());
        } catch (JSONException exception) {
            result = new JSONObject();
        }
        Iterator<String> keys = patch.keys();
        while (keys.hasNext()) {
            String key = keys.next();
            try {
                result.put(key, patch.get(key));
            } catch (JSONException ignored) {
                // 单个字段无法写入时保留原配置。
            }
        }
        return result;
    }

    private static JSONObject makeData(String key, Object value) {
        JSONObject data = new JSONObject();
        try {
            data.put(key, value == null ? JSONObject.NULL : value);
        } catch (JSONException ignored) {
            // 基础值写入不会失败。
        }
        return data;
    }

    private static int readColor(JSONObject spec, String key, int defaultColor) {
        Object value = spec.opt(key);
        if (value instanceof Number) {
            return ((Number) value).intValue();
        }
        if (value instanceof String) {
            try {
                return Color.parseColor((String) value);
            } catch (IllegalArgumentException ignored) {
                return defaultColor;
            }
        }
        return defaultColor;
    }

    private static float clampAlpha(double alpha) {
        return (float) Math.max(0.0, Math.min(1.0, alpha));
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private static final class HudEntry {
        private final long sessionId;
        private final View root;
        private final WindowManager.LayoutParams layoutParams;
        private final JSONObject spec;
        private Runnable timeoutRunnable;

        private HudEntry(long sessionId, View root, WindowManager.LayoutParams layoutParams, JSONObject spec) {
            this.sessionId = sessionId;
            this.root = root;
            this.layoutParams = layoutParams;
            this.spec = spec;
        }
    }
}
