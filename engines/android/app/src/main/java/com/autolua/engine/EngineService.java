package com.autolua.engine;

import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.os.IBinder;

import java.io.IOException;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Android 端脚本运行服务。
 *
 * Activity、悬浮窗和后续其他入口都只向这里发送运行/停止命令，避免每个界面
 * 自己创建脚本线程。该服务运行在 `:engine` 独立进程，和旧项目 NativeService
 * 的职责一致：主进程负责界面，引擎进程负责脚本、HTTP 协议和 root 运行层。
 */
public final class EngineService extends Service {
    public static final String ACTION_RUN_SCRIPT_FILE =
            "com.autolua.engine.action.RUN_SCRIPT_FILE";
    public static final String ACTION_STOP_SCRIPT =
            "com.autolua.engine.action.STOP_SCRIPT";
    public static final String ACTION_PAUSE_SCRIPT =
            "com.autolua.engine.action.PAUSE_SCRIPT";
    public static final String ACTION_RESUME_SCRIPT =
            "com.autolua.engine.action.RESUME_SCRIPT";
    public static final String ACTION_SET_ROOT_MODE =
            "com.autolua.engine.action.SET_ROOT_MODE";
    public static final String ACTION_SAVE_SCREEN_CAPTURE_PERMISSION =
            "com.autolua.engine.action.SAVE_SCREEN_CAPTURE_PERMISSION";
    public static final String ACTION_STATUS =
            "com.autolua.engine.action.STATUS";

    public static final String EXTRA_SCRIPT_PATH = "scriptPath";
    public static final String EXTRA_ENABLED = "enabled";
    public static final String EXTRA_RESULT_CODE = "resultCode";
    public static final String EXTRA_RESULT_DATA = "resultData";
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

    public static void runScriptFile(Context context, String scriptPath) {
        Intent intent = new Intent(context, EngineService.class);
        intent.setAction(ACTION_RUN_SCRIPT_FILE);
        intent.putExtra(EXTRA_SCRIPT_PATH, scriptPath);
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

    public static void setRootModeEnabled(Context context, boolean enabled) {
        Intent intent = new Intent(context, EngineService.class);
        intent.setAction(ACTION_SET_ROOT_MODE);
        intent.putExtra(EXTRA_ENABLED, enabled);
        context.startService(intent);
    }

    public static void saveScreenCapturePermission(Context context, int resultCode, Intent resultData) {
        Intent intent = new Intent(context, EngineService.class);
        intent.setAction(ACTION_SAVE_SCREEN_CAPTURE_PERMISSION);
        intent.putExtra(EXTRA_RESULT_CODE, resultCode);
        intent.putExtra(EXTRA_RESULT_DATA, resultData);
        context.startService(intent);
    }

    @Override
    public void onCreate() {
        super.onCreate();
        ScriptCatalog.ensureScriptDirectory(getApplicationContext());
        ScreenCaptureBridge.init(getApplicationContext());
        NativeEngine.init(getApplicationContext());
        EngineHttpServer.start(getApplicationContext());
        if (EngineSettings.isRootModeEnabled(this)) {
            RootStatus rootStatus = RootShellBridge.prepareRootRuntime();
            if (rootStatus.available) {
                RootHelperBridge.prepare();
            }
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent == null || intent.getAction() == null) {
            return START_STICKY;
        }

        if (ACTION_RUN_SCRIPT_FILE.equals(intent.getAction())) {
            String scriptPath = intent.getStringExtra(EXTRA_SCRIPT_PATH);
            runScriptFileInternal(scriptPath);
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

        if (ACTION_SET_ROOT_MODE.equals(intent.getAction())) {
            boolean enabled = intent.getBooleanExtra(EXTRA_ENABLED, true);
            EngineSettings.setRootModeEnabled(this, enabled);
            if (enabled) {
                RootStatus rootStatus = RootShellBridge.prepareRootRuntime();
                boolean helperReady = rootStatus.available && RootHelperBridge.prepare();
                String message = rootStatus.available
                        ? (helperReady
                                ? "运行模式已切换为 Root 优先，Root 运行层已就绪"
                                : "运行模式已切换为 Root 优先，Root shell 已就绪，截图 helper 未就绪")
                        : "运行模式已切换为 Root 优先，但 Root 权限不可用：" + rootStatus.error;
                broadcastStatus(rootStatus.available ? STATE_FINISHED : STATE_FAILED, message);
            } else {
                broadcastStatus(STATE_FINISHED, "运行模式已切换为无障碍优先");
            }
            return START_STICKY;
        }

        if (ACTION_SAVE_SCREEN_CAPTURE_PERMISSION.equals(intent.getAction())) {
            int resultCode = intent.getIntExtra(EXTRA_RESULT_CODE, 0);
            Intent resultData = intent.getParcelableExtra(EXTRA_RESULT_DATA);
            if (resultData == null) {
                broadcastStatus(STATE_FAILED, "截图授权数据为空");
                return START_STICKY;
            }

            ScreenCaptureBridge.savePermission(resultCode, resultData);
            broadcastStatus(STATE_FINISHED, "截图授权已同步到引擎进程");
            return START_STICKY;
        }

        return START_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    private void runScriptFileInternal(String scriptPath) {
        if (scriptPath == null || scriptPath.isEmpty()) {
            broadcastStatus(STATE_FAILED, "脚本路径为空");
            return;
        }

        ScriptCatalog.ScriptItem item = ScriptCatalog.findByPath(this, scriptPath);
        if (item == null) {
            broadcastStatus(STATE_FAILED, "脚本文件不存在：" + scriptPath);
            return;
        }

        if (!running.compareAndSet(false, true)) {
            broadcastStatus(STATE_RUNNING, "已有脚本正在运行");
            return;
        }

        if (EngineSettings.isRootModeEnabled(this)) {
            // Root 授权只在引擎启动或切换 Root 模式时触发。运行脚本时只读取
            // 已准备好的常驻 root shell，避免每次点击运行都弹出超级用户授权。
            RootStatus rootStatus = RootShellBridge.status();
            if (!rootStatus.available || !RootShellBridge.isRootRuntimeReady()) {
                running.set(false);
                String error = rootStatus.error.isEmpty() ? "Root 权限不可用" : rootStatus.error;
                broadcastStatus(STATE_FAILED, "Root 模式需要授权后才能运行脚本：" + error);
                return;
            }
        }

        ScriptCatalog.setSelectedScript(this, item);
        broadcastStatus(STATE_RUNNING, "脚本运行中：" + item.fileName);

        Thread worker = new Thread(() -> {
            String state = STATE_FINISHED;
            String message;
            try {
                message = NativeEngine.runLuaText(ScriptCatalog.readScriptText(item.filePath));
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

    private void broadcastStatus(String state, String message) {
        Intent intent = new Intent(ACTION_STATUS);
        intent.setPackage(getPackageName());
        intent.putExtra(EXTRA_STATE, state);
        intent.putExtra(EXTRA_MESSAGE, message == null ? "" : message);
        sendBroadcast(intent);
    }
}
