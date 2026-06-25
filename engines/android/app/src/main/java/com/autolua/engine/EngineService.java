package com.autolua.engine;

import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.os.IBinder;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Android 端脚本运行服务。
 *
 * Activity、悬浮窗和后续其他入口都只向这里发送运行/停止命令，避免每个界面
 * 自己创建脚本线程。当前仍与主 App 同进程运行；独立 `:engine` 进程后续再评估。
 */
public final class EngineService extends Service {
    public static final String ACTION_RUN_ASSET =
            "com.autolua.engine.action.RUN_ASSET";
    public static final String ACTION_STOP_SCRIPT =
            "com.autolua.engine.action.STOP_SCRIPT";
    public static final String ACTION_PAUSE_SCRIPT =
            "com.autolua.engine.action.PAUSE_SCRIPT";
    public static final String ACTION_RESUME_SCRIPT =
            "com.autolua.engine.action.RESUME_SCRIPT";
    public static final String ACTION_STATUS =
            "com.autolua.engine.action.STATUS";

    public static final String EXTRA_ASSET_PATH = "assetPath";
    public static final String EXTRA_STATE = "state";
    public static final String EXTRA_MESSAGE = "message";

    public static final String STATE_RUNNING = "running";
    public static final String STATE_PAUSING = "pausing";
    public static final String STATE_PAUSED = "paused";
    public static final String STATE_STOPPING = "stopping";
    public static final String STATE_FINISHED = "finished";
    public static final String STATE_FAILED = "failed";

    private final AtomicBoolean running = new AtomicBoolean(false);

    public static void ensureStarted(Context context) {
        Intent intent = new Intent(context, EngineService.class);
        context.startService(intent);
    }

    public static void runAssetScript(Context context, String assetPath) {
        Intent intent = new Intent(context, EngineService.class);
        intent.setAction(ACTION_RUN_ASSET);
        intent.putExtra(EXTRA_ASSET_PATH, assetPath);
        context.startService(intent);
    }

    public static void stopScript(Context context) {
        Intent intent = new Intent(context, EngineService.class);
        intent.setAction(ACTION_STOP_SCRIPT);
        context.startService(intent);
    }

    public static void pauseScript(Context context) {
        Intent intent = new Intent(context, EngineService.class);
        intent.setAction(ACTION_PAUSE_SCRIPT);
        context.startService(intent);
    }

    public static void resumeScript(Context context) {
        Intent intent = new Intent(context, EngineService.class);
        intent.setAction(ACTION_RESUME_SCRIPT);
        context.startService(intent);
    }

    @Override
    public void onCreate() {
        super.onCreate();
        NativeEngine.init(getApplicationContext());
        EngineHttpServer.start(getApplicationContext());
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent == null || intent.getAction() == null) {
            return START_STICKY;
        }

        if (ACTION_RUN_ASSET.equals(intent.getAction())) {
            String assetPath = intent.getStringExtra(EXTRA_ASSET_PATH);
            runAssetScriptInternal(assetPath);
            return START_STICKY;
        }

        if (ACTION_STOP_SCRIPT.equals(intent.getAction())) {
            NativeEngine.stop();
            broadcastStatus(STATE_STOPPING, "已请求停止脚本");
            return START_STICKY;
        }

        if (ACTION_PAUSE_SCRIPT.equals(intent.getAction())) {
            boolean accepted = NativeEngine.pause();
            broadcastStatus(
                    accepted ? STATE_PAUSING : STATE_FAILED,
                    accepted ? "已请求暂停脚本" : "当前没有可暂停的脚本"
            );
            return START_STICKY;
        }

        if (ACTION_RESUME_SCRIPT.equals(intent.getAction())) {
            boolean accepted = NativeEngine.resume();
            broadcastStatus(
                    accepted ? STATE_RUNNING : STATE_FAILED,
                    accepted ? "已请求继续脚本" : "当前没有已暂停的脚本"
            );
            return START_STICKY;
        }

        return START_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    private void runAssetScriptInternal(String assetPath) {
        if (assetPath == null || assetPath.isEmpty()) {
            broadcastStatus(STATE_FAILED, "脚本路径为空");
            return;
        }

        if (!running.compareAndSet(false, true)) {
            broadcastStatus(STATE_RUNNING, "已有脚本正在运行");
            return;
        }

        if (EngineSettings.isRootModeEnabled(this) && !RootShellBridge.isRootAvailable()) {
            running.set(false);
            RootStatus rootStatus = RootShellBridge.status();
            String error = rootStatus.error.isEmpty() ? "Root 权限不可用" : rootStatus.error;
            broadcastStatus(STATE_FAILED, "Root 模式需要授权后才能运行脚本：" + error);
            return;
        }

        ScriptCatalog.ScriptItem item = ScriptCatalog.findByPath(assetPath);
        broadcastStatus(STATE_RUNNING, "脚本运行中：" + item.title);

        Thread worker = new Thread(() -> {
            String state = STATE_FINISHED;
            String message;
            try {
                message = NativeEngine.runLuaText(readAssetText(assetPath));
                if (message.contains("failed") || message.contains("Engine is already running")) {
                    state = STATE_FAILED;
                }
            } catch (IOException exception) {
                state = STATE_FAILED;
                message = "读取脚本失败：" + exception.getMessage();
            } finally {
                running.set(false);
            }

            broadcastStatus(state, message);
        }, "EngineServiceLuaWorker");
        worker.start();
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

    private void broadcastStatus(String state, String message) {
        Intent intent = new Intent(ACTION_STATUS);
        intent.setPackage(getPackageName());
        intent.putExtra(EXTRA_STATE, state);
        intent.putExtra(EXTRA_MESSAGE, message == null ? "" : message);
        sendBroadcast(intent);
    }
}
