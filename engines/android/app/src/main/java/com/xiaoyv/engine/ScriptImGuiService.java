/**
 * 文件用途：在 :engine 进程创建 Dear ImGui 透明悬浮 Surface，并转发触摸、按键和输入法。
 */
package com.xiaoyv.engine;

import android.app.ActivityManager;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.PixelFormat;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.provider.Settings;
import android.text.InputType;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowManager;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;
import android.widget.FrameLayout;

import org.json.JSONObject;

import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Dear ImGui Android Surface 宿主。
 *
 * Service 与 LuaRuntime 同在 `:engine` 进程，但 Java 主线程只维护 WindowManager 和
 * Surface 生命周期。EGL、OpenGL、Dear ImGui 控件与纹理全部由 libengine.so 的独立
 * 渲染线程管理，Java 不使用原生控件伪装脚本控件。
 */
public final class ScriptImGuiService extends Service {
    public static final String ACTION_SHOW = "com.xiaoyv.engine.action.IMGUI_SHOW";
    public static final String ACTION_UPDATE = "com.xiaoyv.engine.action.IMGUI_UPDATE";
    public static final String ACTION_CLOSE = "com.xiaoyv.engine.action.IMGUI_CLOSE";
    private static final String EXTRA_CONFIG_JSON = "configJson";

    private static volatile ScriptImGuiService activeInstance;
    /**
     * 标记 SHOW/UPDATE 已提交但 Service 可能尚未执行 onCreate 的短暂窗口。
     *
     * 关闭命令据此决定是否需要真正 startService，既不会漏掉与 SHOW 并发的关闭，也避免
     * 普通非 ImGui 脚本结束时仅为了处理一个空 CLOSE 而创建 Service。
     */
    private static final AtomicBoolean surfaceRequested = new AtomicBoolean(false);

    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private WindowManager windowManager;
    private FrameLayout rootView;
    private SurfaceView surfaceView;
    private ImGuiInputView inputView;
    private WindowManager.LayoutParams layoutParams;
    private JSONObject config = new JSONObject();
    private int activePointerId = MotionEvent.INVALID_POINTER_ID;
    private boolean closeRequested;

    /**
     * 当前 Surface 生命周期代次。
     *
     * CLOSE 的停止任务会记录本值；若任务执行前收到新的 SHOW，SHOW 会递增代次，旧任务
     * 因此只能清理旧 Surface，不能再停止承载新 Surface 的同一个 Service 实例。
     */
    private long lifecycleGeneration;

    /** 查询设备声明的 OpenGL ES 版本，避免在不支持 GLES3 时启动渲染线程。 */
    public static boolean isSupported(Context context) {
        if (context == null) {
            return false;
        }
        ActivityManager manager =
                (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
        return manager != null
                && manager.getDeviceConfigurationInfo() != null
                && manager.getDeviceConfigurationInfo().reqGlEsVersion >= 0x00030000;
    }

    /**
     * 从引擎线程向当前进程的 Service 发送显示、更新或关闭命令。
     *
     * 只有显示和更新需要悬浮窗权限；关闭必须始终可送达，保证脚本停止时不会遗留 Surface。
     */
    public static synchronized boolean sendCommand(
            Context context,
            String action,
            String configJson
    ) {
        if (context == null || action == null || action.isEmpty()) {
            return false;
        }
        if (!ACTION_CLOSE.equals(action)
                && Build.VERSION.SDK_INT >= Build.VERSION_CODES.M
                && !Settings.canDrawOverlays(context)) {
            return false;
        }

        if (ACTION_CLOSE.equals(action)) {
            boolean hadPendingOrActive = surfaceRequested.getAndSet(false)
                    || activeInstance != null;
            if (!hadPendingOrActive) {
                return true;
            }
        } else if (ACTION_SHOW.equals(action)) {
            surfaceRequested.set(true);
        } else if (ACTION_UPDATE.equals(action) && !surfaceRequested.get()) {
            // UPDATE 只能修改一个已经请求显示的 Surface，不能在 CLOSE 后重新创建旧窗口。
            return false;
        }

        Intent intent = new Intent(context, ScriptImGuiService.class);
        intent.setAction(action);
        intent.putExtra(EXTRA_CONFIG_JSON, configJson == null ? "{}" : configJson);
        try {
            context.startService(intent);
            return true;
        } catch (RuntimeException exception) {
            if (!ACTION_CLOSE.equals(action)) {
                surfaceRequested.set(false);
            }
            return false;
        }
    }

    /** 渲染线程只在 WantTextInput 变化时调用；实际输入法操作切回 Android 主线程。 */
    public static boolean setKeyboardVisible(boolean visible) {
        ScriptImGuiService service = activeInstance;
        if (service == null) {
            return !visible;
        }
        service.mainHandler.post(() -> service.applyKeyboardVisible(visible));
        return true;
    }

    @Override
    public void onCreate() {
        super.onCreate();
        activeInstance = this;
        windowManager = (WindowManager) getSystemService(WINDOW_SERVICE);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent == null || intent.getAction() == null) {
            return START_NOT_STICKY;
        }
        String action = intent.getAction();
        if (ACTION_CLOSE.equals(action)) {
            closeRequested = true;
            surfaceRequested.set(false);
            closeSurface();
            scheduleStopAfterClose(startId);
            return START_NOT_STICKY;
        }

        JSONObject incoming = parseConfig(intent.getStringExtra(EXTRA_CONFIG_JSON));
        if (ACTION_SHOW.equals(action)) {
            lifecycleGeneration++;
            closeRequested = false;
            surfaceRequested.set(true);
            showSurface(incoming);
        } else if (ACTION_UPDATE.equals(action)) {
            if (!surfaceRequested.get()) {
                return START_NOT_STICKY;
            }
            updateSurface(incoming);
        }
        return START_NOT_STICKY;
    }

    /**
     * 在当前主线程消息处理结束后尝试停止空闲 Service。
     *
     * 不能在 CLOSE 分支直接 stopSelf：脚本可能紧接着重新 SHOW，而 Android 仍会复用当前
     * Service 实例。stopSelfResult 还会核对 startId，确保已经提交但尚未分发的新命令不会
     * 被较早的关闭命令吞掉；生命周期代次则保护已经分发并创建的新 Surface。
     */
    private void scheduleStopAfterClose(int closeStartId) {
        final long closeGeneration = lifecycleGeneration;
        mainHandler.post(() -> {
            if (surfaceRequested.get() || lifecycleGeneration != closeGeneration) {
                return;
            }
            stopSelfResult(closeStartId);
        });
    }

    @Override
    public void onDestroy() {
        if (rootView != null && !closeRequested) {
            NativeEngine.notifyImGuiSurfaceFailure("ImGui Surface 服务被系统终止");
        }
        surfaceRequested.set(false);
        closeSurface();
        if (activeInstance == this) {
            activeInstance = null;
        }
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    /** 创建 SurfaceView；重复 show 只更新同一个悬浮窗口，不增加第二条渲染线程。 */
    private void showSurface(JSONObject incoming) {
        config = incoming;
        if (windowManager == null) {
            NativeEngine.notifyImGuiSurfaceFailure("系统窗口管理器不可用");
            return;
        }
        if (rootView != null) {
            applyLayoutConfig();
            return;
        }

        rootView = new FrameLayout(this);
        rootView.setBackgroundColor(Color.TRANSPARENT);
        rootView.setFocusable(true);
        rootView.setFocusableInTouchMode(true);

        surfaceView = new SurfaceView(this);
        surfaceView.setZOrderOnTop(true);
        surfaceView.getHolder().setFormat(PixelFormat.TRANSLUCENT);
        surfaceView.getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override
            public void surfaceCreated(SurfaceHolder holder) {
                if (!NativeEngine.attachImGuiSurface(holder.getSurface())) {
                    NativeEngine.notifyImGuiSurfaceFailure("ImGui Surface 附着失败");
                }
            }

            @Override
            public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
                // Dear ImGui Android backend 每帧直接读取 ANativeWindow 尺寸，无需复制宽高。
            }

            @Override
            public void surfaceDestroyed(SurfaceHolder holder) {
                NativeEngine.detachImGuiSurface();
            }
        });
        installPointerInput(surfaceView);
        rootView.addView(surfaceView, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT
        ));

        // 该 1x1 透明 View 只提供 InputConnection。脚本可见内容始终由 Dear ImGui 绘制。
        inputView = new ImGuiInputView(this);
        FrameLayout.LayoutParams inputParams = new FrameLayout.LayoutParams(1, 1);
        inputParams.gravity = Gravity.TOP | Gravity.START;
        rootView.addView(inputView, inputParams);

        rootView.setOnKeyListener((view, keyCode, event) -> {
            forwardKeyEvent(event);
            return true;
        });

        layoutParams = createLayoutParams();
        try {
            windowManager.addView(rootView, layoutParams);
            if (isTouchable()) {
                rootView.requestFocus();
            }
        } catch (RuntimeException exception) {
            NativeEngine.notifyImGuiSurfaceFailure(
                    "创建 ImGui 悬浮窗口失败：" + exception.getClass().getSimpleName()
            );
            closeSurface();
        }
    }

    /** 更新外层 WindowManager 几何和触摸标志；Surface 与 EGL context 保持不变。 */
    private void updateSurface(JSONObject incoming) {
        config = incoming;
        if (rootView == null) {
            showSurface(incoming);
            return;
        }
        applyLayoutConfig();
    }

    private void applyLayoutConfig() {
        if (rootView == null || windowManager == null || layoutParams == null) {
            return;
        }
        WindowManager.LayoutParams replacement = createLayoutParams();
        layoutParams.width = replacement.width;
        layoutParams.height = replacement.height;
        layoutParams.x = replacement.x;
        layoutParams.y = replacement.y;
        layoutParams.flags = replacement.flags;
        layoutParams.softInputMode = replacement.softInputMode;
        try {
            windowManager.updateViewLayout(rootView, layoutParams);
            if (isTouchable()) {
                rootView.requestFocus();
            } else {
                applyKeyboardVisible(false);
            }
        } catch (RuntimeException ignored) {
            NativeEngine.notifyImGuiSurfaceFailure("更新 ImGui 悬浮窗口失败");
            closeSurface();
        }
    }

    private WindowManager.LayoutParams createLayoutParams() {
        int type = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                : WindowManager.LayoutParams.TYPE_PHONE;
        boolean windowed = config.optBoolean("windowed", false);
        boolean touchable = isTouchable();
        int flags = WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN
                | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS
                | WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL;
        if (!touchable) {
            flags |= WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE
                    | WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE;
        }

        int width = windowed
                ? Math.max(1, config.optInt("width", 600))
                : WindowManager.LayoutParams.MATCH_PARENT;
        int height = windowed
                ? Math.max(1, config.optInt("height", 600))
                : WindowManager.LayoutParams.MATCH_PARENT;
        WindowManager.LayoutParams params = new WindowManager.LayoutParams(
                width,
                height,
                type,
                flags,
                PixelFormat.TRANSLUCENT
        );
        params.gravity = Gravity.TOP | Gravity.START;
        params.x = windowed ? config.optInt("x", 0) : 0;
        params.y = windowed ? config.optInt("y", 0) : 0;
        params.softInputMode = WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE;
        return params;
    }

    /**
     * 只转发首个按下的手指。
     *
     * ACTION_POINTER_UP 必须转换为 ACTION_UP，否则首指先抬起时 Dear ImGui 会一直保留
     * 鼠标按下状态；其余新增手指不会改变当前活动指针。
     */
    private void installPointerInput(SurfaceView view) {
        view.setOnTouchListener((target, event) -> {
            int action = event.getActionMasked();
            if (action == MotionEvent.ACTION_DOWN) {
                activePointerId = event.getPointerId(event.getActionIndex());
            } else if (action == MotionEvent.ACTION_POINTER_DOWN) {
                return true;
            }

            int pointerIndex = activePointerId == MotionEvent.INVALID_POINTER_ID
                    ? event.getActionIndex()
                    : event.findPointerIndex(activePointerId);
            if (pointerIndex < 0 || pointerIndex >= event.getPointerCount()) {
                return true;
            }

            int forwardedAction = action;
            if (action == MotionEvent.ACTION_POINTER_UP) {
                int releasedPointerId = event.getPointerId(event.getActionIndex());
                if (releasedPointerId != activePointerId) {
                    return true;
                }
                pointerIndex = event.getActionIndex();
                forwardedAction = MotionEvent.ACTION_UP;
            }
            NativeEngine.enqueueImGuiTouch(
                    forwardedAction,
                    event.getPointerId(pointerIndex),
                    event.getX(pointerIndex),
                    event.getY(pointerIndex)
            );
            if (forwardedAction == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
                activePointerId = MotionEvent.INVALID_POINTER_ID;
            }
            return true;
        });
        view.setOnGenericMotionListener((target, event) -> {
            if ((event.getSource() & android.view.InputDevice.SOURCE_CLASS_POINTER) == 0
                    || event.getActionMasked() != MotionEvent.ACTION_SCROLL) {
                return false;
            }
            NativeEngine.enqueueImGuiScroll(
                    event.getAxisValue(MotionEvent.AXIS_HSCROLL),
                    event.getAxisValue(MotionEvent.AXIS_VSCROLL)
            );
            return true;
        });
    }

    private void forwardKeyEvent(KeyEvent event) {
        if (event == null) {
            return;
        }
        NativeEngine.enqueueImGuiKey(
                event.getAction(),
                event.getKeyCode(),
                event.getUnicodeChar(event.getMetaState()),
                event.getMetaState()
        );
    }

    private void applyKeyboardVisible(boolean visible) {
        if (inputView == null || !isTouchable()) {
            return;
        }
        InputMethodManager manager = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
        if (manager == null) {
            return;
        }
        if (visible) {
            inputView.requestFocus();
            manager.restartInput(inputView);
            manager.showSoftInput(inputView, InputMethodManager.SHOW_IMPLICIT);
        } else {
            manager.hideSoftInputFromWindow(inputView.getWindowToken(), 0);
            rootView.requestFocus();
        }
    }

    private void closeSurface() {
        applyKeyboardVisible(false);
        if (rootView != null && windowManager != null) {
            try {
                windowManager.removeViewImmediate(rootView);
            } catch (RuntimeException ignored) {
                NativeEngine.detachImGuiSurface();
            }
        } else {
            NativeEngine.detachImGuiSurface();
        }
        rootView = null;
        surfaceView = null;
        inputView = null;
        layoutParams = null;
        activePointerId = MotionEvent.INVALID_POINTER_ID;
    }

    private boolean isTouchable() {
        return config.optBoolean("touchable", true);
    }

    private static JSONObject parseConfig(String text) {
        try {
            return new JSONObject(text == null || text.trim().isEmpty() ? "{}" : text);
        } catch (Exception exception) {
            return new JSONObject();
        }
    }

    /**
     * 透明输入代理。
     *
     * IME 的候选和组合文本保留在 BaseInputConnection；只有最终 commitText 才发送给 Dear
     * ImGui，避免中文输入法每次更新 composingText 时把候选内容重复插入。
     */
    private static final class ImGuiInputView extends View {
        private final InputConnection connection;

        ImGuiInputView(Context context) {
            super(context);
            setFocusable(true);
            setFocusableInTouchMode(true);
            setBackgroundColor(Color.TRANSPARENT);
            connection = new BaseInputConnection(this, true) {
                @Override
                public boolean commitText(CharSequence text, int newCursorPosition) {
                    if (text != null && text.length() > 0) {
                        NativeEngine.enqueueImGuiText(text.toString());
                    }
                    return true;
                }

                @Override
                public boolean deleteSurroundingText(int beforeLength, int afterLength) {
                    int count = Math.max(1, beforeLength);
                    for (int index = 0; index < count; index++) {
                        NativeEngine.enqueueImGuiKey(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_DEL, 0, 0);
                        NativeEngine.enqueueImGuiKey(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_DEL, 0, 0);
                    }
                    return true;
                }

                @Override
                public boolean sendKeyEvent(KeyEvent event) {
                    if (event != null) {
                        NativeEngine.enqueueImGuiKey(
                                event.getAction(),
                                event.getKeyCode(),
                                event.getUnicodeChar(event.getMetaState()),
                                event.getMetaState()
                        );
                    }
                    return true;
                }
            };
        }

        @Override
        public boolean onCheckIsTextEditor() {
            return true;
        }

        @Override
        public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
            outAttrs.inputType = InputType.TYPE_CLASS_TEXT
                    | InputType.TYPE_TEXT_FLAG_MULTI_LINE
                    | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS;
            outAttrs.imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI;
            return connection;
        }

        @Override
        public boolean onKeyDown(int keyCode, KeyEvent event) {
            NativeEngine.enqueueImGuiKey(
                    event.getAction(),
                    keyCode,
                    event.getUnicodeChar(event.getMetaState()),
                    event.getMetaState()
            );
            return true;
        }

        @Override
        public boolean onKeyUp(int keyCode, KeyEvent event) {
            NativeEngine.enqueueImGuiKey(
                    event.getAction(),
                    keyCode,
                    event.getUnicodeChar(event.getMetaState()),
                    event.getMetaState()
            );
            return true;
        }
    }
}
