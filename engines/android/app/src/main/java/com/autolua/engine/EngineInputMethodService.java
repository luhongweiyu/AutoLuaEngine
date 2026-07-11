/**
 * 文件用途：提供 AutoLuaEngine 专用输入法服务，向当前焦点输入框提交 Unicode 文本。
 */
package com.autolua.engine;

import android.inputmethodservice.InputMethodService;
import android.os.Handler;
import android.os.Looper;
import android.view.inputmethod.InputConnection;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * AutoLuaEngine 的无界面输入法。
 *
 * 该服务不绘制键盘，只在 imeLib.lock() 将它设为默认输入法后，使用当前
 * InputConnection.commitText 提交文本。因此中文、Emoji 和组合文本不再依赖 Root
 * 按键注入。脚本运行在同一个 :engine 进程，但仍统一切到主线程访问输入法对象。
 */
public final class EngineInputMethodService extends InputMethodService {
    private static final Object INSTANCE_LOCK = new Object();
    private static final long COMMIT_TIMEOUT_MS = 1500L;

    private static EngineInputMethodService instance;

    private Handler mainHandler;

    /**
     * 输入法服务创建后登记当前实例，供 :engine 脚本进程同步调用。
     */
    @Override
    public void onCreate() {
        super.onCreate();
        mainHandler = new Handler(Looper.getMainLooper());
        synchronized (INSTANCE_LOCK) {
            instance = this;
        }
    }

    /**
     * 服务销毁时清理全局入口，避免脚本对失效 InputConnection 提交文本。
     */
    @Override
    public void onDestroy() {
        synchronized (INSTANCE_LOCK) {
            if (instance == this) {
                instance = null;
            }
        }
        super.onDestroy();
    }

    /**
     * 当前输入法不显示键盘视图，只作为脚本 Unicode 文本提交通道。
     */
    @Override
    public boolean onEvaluateInputViewShown() {
        return false;
    }

    /**
     * 向当前焦点输入框提交文本。
     *
     * 返回 false 表示输入法尚未成为活动服务，或当前没有可写 InputConnection。
     * 调用者应先 lock，并确保目标输入框已获得焦点。
     */
    public static boolean commitText(String text) {
        EngineInputMethodService service;
        synchronized (INSTANCE_LOCK) {
            service = instance;
        }
        if (service == null || service.mainHandler == null) {
            return false;
        }
        return service.commitTextOnMainThread(text == null ? "" : text);
    }

    /**
     * 将 commitText 切换到输入法主线程并等待实际结果。
     */
    private boolean commitTextOnMainThread(String text) {
        if (Looper.myLooper() == mainHandler.getLooper()) {
            return commitTextDirectly(text);
        }

        AtomicBoolean result = new AtomicBoolean(false);
        CountDownLatch completed = new CountDownLatch(1);
        boolean posted = mainHandler.post(() -> {
            try {
                result.set(commitTextDirectly(text));
            } finally {
                completed.countDown();
            }
        });
        if (!posted) {
            return false;
        }

        try {
            return completed.await(COMMIT_TIMEOUT_MS, TimeUnit.MILLISECONDS) && result.get();
        } catch (InterruptedException exception) {
            Thread.currentThread().interrupt();
            return false;
        }
    }

    /**
     * 在输入法主线程访问当前 InputConnection 并提交完整 Unicode 文本。
     */
    private boolean commitTextDirectly(String text) {
        try {
            InputConnection connection = getCurrentInputConnection();
            return connection != null && connection.commitText(text, 1);
        } catch (RuntimeException exception) {
            return false;
        }
    }
}
