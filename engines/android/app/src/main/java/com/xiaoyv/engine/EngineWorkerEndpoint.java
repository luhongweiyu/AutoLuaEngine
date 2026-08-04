/**
 * 文件用途：把一次性脚本 Worker 内的 NativeEngine 封装为 Binder 端点。
 */
package com.xiaoyv.engine;

import android.content.Context;
import android.os.ParcelFileDescriptor;
import android.view.Surface;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicBoolean;

/** Root 与非 Root Worker 共用同一份 native、Java 互操作和 ImGui 入口。 */
final class EngineWorkerEndpoint extends IEngineWorker.Stub {
    private static final int MAX_COMMAND_BYTES = 16 * 1024 * 1024;

    interface ShutdownCallback {
        void onShutdown();
    }

    private final Context appContext;
    private final ShutdownCallback shutdownCallback;
    private final AtomicBoolean closed = new AtomicBoolean(false);

    EngineWorkerEndpoint(
            Context context,
            String nativeLibraryDirectory,
            ShutdownCallback shutdownCallback
    ) {
        Context application = context.getApplicationContext();
        appContext = application == null ? context : application;
        this.shutdownCallback = shutdownCallback;
        NativeEngine.loadLibrary(nativeLibraryDirectory);
        NativeEngine.init(appContext);
    }

    @Override
    public ParcelFileDescriptor callJsonPipe(
            String method,
            ParcelFileDescriptor paramsJson
    ) {
        ensureOpen();
        String params = readUtf8(paramsJson);
        String response = NativeEngine.callJson(method, params);
        return writeBytes(response.getBytes(StandardCharsets.UTF_8), "EngineWorkerJsonReply");
    }

    @Override
    public ParcelFileDescriptor openScreenFrame() {
        ensureOpen();
        byte[] frame = NativeEngine.getScreenFrame();
        return writeBytes(frame == null ? new byte[0] : frame, "EngineWorkerScreenFrame");
    }

    @Override
    public boolean attachImGuiSurface(Surface surface) {
        ensureOpen();
        return NativeEngine.attachImGuiSurface(surface);
    }

    @Override
    public void detachImGuiSurface() {
        if (!closed.get()) {
            NativeEngine.detachImGuiSurface();
        }
    }

    @Override
    public void notifyImGuiSurfaceFailure(String message) {
        if (!closed.get()) {
            NativeEngine.notifyImGuiSurfaceFailure(message);
        }
    }

    @Override
    public void enqueueImGuiTouch(int action, int pointerId, float x, float y) {
        if (!closed.get()) {
            NativeEngine.enqueueImGuiTouch(action, pointerId, x, y);
        }
    }

    @Override
    public void enqueueImGuiText(String text) {
        if (!closed.get()) {
            NativeEngine.enqueueImGuiText(text);
        }
    }

    @Override
    public void enqueueImGuiKey(
            int action,
            int keyCode,
            int unicodeCodePoint,
            int metaState
    ) {
        if (!closed.get()) {
            NativeEngine.enqueueImGuiKey(action, keyCode, unicodeCodePoint, metaState);
        }
    }

    @Override
    public void enqueueImGuiScroll(float horizontal, float vertical) {
        if (!closed.get()) {
            NativeEngine.enqueueImGuiScroll(horizontal, vertical);
        }
    }

    @Override
    public int processId() {
        return android.os.Process.myPid();
    }

    @Override
    public int processUid() {
        return android.os.Process.myUid();
    }

    @Override
    public void shutdown() {
        close();
    }

    void close() {
        if (!closed.compareAndSet(false, true)) {
            return;
        }
        try {
            NativeEngine.callJson("ui.closeAll", "{}");
        } catch (RuntimeException ignored) {
            // Worker 即将退出，清理命令失败不阻止内核回收全部进程资源。
        }
        try {
            NativeEngine.detachImGuiSurface();
        } catch (RuntimeException ignored) {
            // Surface 可能从未创建。
        }
        try {
            NetworkPlatformBridge.closeAllWebSockets();
        } catch (Throwable ignored) {
            // 未使用网络能力时不为清理动作初始化第三方网络栈。
        }
        try {
            AccessibilityNodePlatformBridge.releaseScriptState(appContext);
        } catch (Throwable ignored) {
            // App 宿主可能已经退出。
        }
        try {
            AndroidHostBridge.closeAllScriptUi();
        } catch (Throwable ignored) {
            // App 宿主可能已经退出。
        }
        try {
            MediaProjectionScreenCaptureBridge.shutdown();
        } catch (Throwable ignored) {
            // Root Worker 不初始化非 Root 截图桥；本地宿主也可能已经退出。
        }
        RootHelperBridge.shutdown();
        if (shutdownCallback != null) {
            shutdownCallback.onShutdown();
        }
    }

    private void ensureOpen() {
        if (closed.get()) {
            throw new IllegalStateException("脚本 Worker 已关闭");
        }
    }

    private static String readUtf8(ParcelFileDescriptor descriptor) {
        if (descriptor == null) {
            return "{}";
        }
        try (InputStream input = new ParcelFileDescriptor.AutoCloseInputStream(descriptor);
             ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[8192];
            int count;
            while ((count = input.read(buffer)) != -1) {
                if (output.size() + count > MAX_COMMAND_BYTES) {
                    throw new IllegalArgumentException("Worker 命令参数过大");
                }
                output.write(buffer, 0, count);
            }
            return output.toString(StandardCharsets.UTF_8.name());
        } catch (IOException exception) {
            throw new IllegalStateException("读取 Worker 命令失败", exception);
        }
    }

    private static ParcelFileDescriptor writeBytes(byte[] bytes, String threadName) {
        try {
            ParcelFileDescriptor[] pipe = ParcelFileDescriptor.createPipe();
            Thread writer = new Thread(() -> {
                try (OutputStream output =
                             new ParcelFileDescriptor.AutoCloseOutputStream(pipe[1])) {
                    output.write(bytes);
                } catch (IOException ignored) {
                    // 控制端提前断开时，当前响应自然作废。
                }
            }, threadName);
            writer.setDaemon(true);
            writer.start();
            return pipe[0];
        } catch (IOException exception) {
            throw new IllegalStateException("创建 Worker 数据管道失败", exception);
        }
    }
}
