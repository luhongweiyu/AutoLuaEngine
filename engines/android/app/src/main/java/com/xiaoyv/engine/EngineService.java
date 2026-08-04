/**
 * 文件用途：:engine 常驻控制服务，负责一次性 Worker 会话、HTTP 和状态广播。
 */
package com.xiaoyv.engine;

import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.os.IBinder;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.IOException;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Android 端脚本运行服务。
 *
 * Activity、悬浮窗和后续其他入口都只向这里发送运行/停止命令，避免每个界面
 * 自己创建脚本线程。该服务运行在常驻 `:engine` 控制进程；真正的语言运行时、
 * libengine.so、FFI 和扩展位于每次运行新建的 Root/非 Root Worker。
 */
public final class EngineService extends Service {
    public static final String ACTION_RUN_SCRIPT_FILE =
            "com.xiaoyv.engine.action.RUN_SCRIPT_FILE";
    public static final String ACTION_STOP_SCRIPT =
            "com.xiaoyv.engine.action.STOP_SCRIPT";
    public static final String ACTION_PAUSE_SCRIPT =
            "com.xiaoyv.engine.action.PAUSE_SCRIPT";
    public static final String ACTION_RESUME_SCRIPT =
            "com.xiaoyv.engine.action.RESUME_SCRIPT";
    public static final String ACTION_RESTART_SCRIPT =
            "com.xiaoyv.engine.action.RESTART_SCRIPT";
    public static final String ACTION_FORCE_STOP_ENGINE_PROCESS =
            "com.xiaoyv.engine.action.FORCE_STOP_ENGINE_PROCESS";
    public static final String ACTION_SYNC_ROOT_MODE =
            "com.xiaoyv.engine.action.SYNC_ROOT_MODE";
    public static final String ACTION_STATUS =
            "com.xiaoyv.engine.action.STATUS";

    public static final String EXTRA_SCRIPT_PATH = "scriptPath";
    public static final String EXTRA_ROOT_MODE_ENABLED = "rootModeEnabled";
    public static final String EXTRA_STATE = "state";
    public static final String EXTRA_MESSAGE = "message";

    public static final String STATE_RUNNING = "running";
    public static final String STATE_PAUSING = "pausing";
    public static final String STATE_PAUSED = "paused";
    public static final String STATE_STOPPING = "stopping";
    public static final String STATE_FINISHED = "finished";
    public static final String STATE_FAILED = "failed";

    private final AtomicBoolean running = new AtomicBoolean(false);
    private final AtomicBoolean restartRequested = new AtomicBoolean(false);
    private final AtomicBoolean forceStopRequested = new AtomicBoolean(false);
    private volatile String activeScriptPath;
    private volatile String restartScriptPath;

    public static void ensureStarted(Context context) {
        Intent intent = new Intent(context, EngineService.class);
        context.startService(intent);
    }

    /**
     * 请求引擎运行共享脚本目录中的真实文件路径。
     */
    public static void runScriptFile(Context context, String scriptPath) {
        Intent intent = new Intent(context, EngineService.class);
        intent.setAction(ACTION_RUN_SCRIPT_FILE);
        intent.putExtra(EXTRA_SCRIPT_PATH, scriptPath);
        context.startService(intent);
    }

    /**
     * 从所有手机端控制入口运行当前选中的脚本。
     *
     * 主界面、悬浮控制和后续快捷入口共用这里的选择与格式校验，避免同一文件从不同入口
     * 得到不同提示。服务收到 Intent 后仍会按磁盘真实状态做第二次校验。
     */
    public static RunRequestResult runSelectedScript(Context context) {
        ScriptCatalog.ScriptItem item = ScriptCatalog.getSelectedScript(context);
        if (item == null) {
            return RunRequestResult.rejected("脚本目录为空，无法运行");
        }
        if (!item.runnable) {
            return RunRequestResult.rejected("不支持运行该文件格式：" + item.fileName);
        }
        if (!EngineSettings.isRootModeEnabled(context)
                && !MediaProjectionCaptureService.isProjectionReady()) {
            MainActivity.requestNonRootScreenCaptureAndRun(context, item.filePath);
            return RunRequestResult.rejected("请允许非 Root 屏幕读取后运行脚本");
        }

        runScriptFile(context, item.filePath);
        return RunRequestResult.started("已发送运行命令：" + item.fileName);
    }

    public static void stopScript(Context context) {
        Intent intent = new Intent(context, EngineService.class);
        intent.setAction(ACTION_STOP_SCRIPT);
        context.startService(intent);
    }

    public static void forceStopEngineProcess(Context context) {
        Intent intent = new Intent(context, EngineService.class);
        intent.setAction(ACTION_FORCE_STOP_ENGINE_PROCESS);
        context.startService(intent);
    }

    /** 把主进程设置页的模式切换显式同步到常驻 :engine 进程。 */
    public static void syncRootMode(Context context, boolean enabled) {
        Intent intent = new Intent(context, EngineService.class);
        intent.setAction(ACTION_SYNC_ROOT_MODE);
        intent.putExtra(EXTRA_ROOT_MODE_ENABLED, enabled);
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

    public static void restartScript(Context context) {
        Intent intent = new Intent(context, EngineService.class);
        intent.setAction(ACTION_RESTART_SCRIPT);
        context.startService(intent);
    }

    public static final class RunRequestResult {
        public final boolean started;
        public final String message;

        private RunRequestResult(boolean started, String message) {
            this.started = started;
            this.message = message;
        }

        private static RunRequestResult started(String message) {
            return new RunRequestResult(true, message);
        }

        private static RunRequestResult rejected(String message) {
            return new RunRequestResult(false, message);
        }
    }

    @Override
    public void onCreate() {
        super.onCreate();
        AndroidHostBridge.init(getApplicationContext());
        EngineWorkerCoordinator.initialize(getApplicationContext());
        EngineHttpServer.start(getApplicationContext());
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
            requestScriptStop();
            return START_STICKY;
        }

        if (ACTION_FORCE_STOP_ENGINE_PROCESS.equals(intent.getAction())) {
            forceStopRequested.set(true);
            restartRequested.set(false);
            restartScriptPath = null;
            EngineWorkerCoordinator.forceStop();
            broadcastStatus(STATE_FINISHED, "已强制停止脚本 Worker");
            return START_STICKY;
        }

        if (ACTION_SYNC_ROOT_MODE.equals(intent.getAction())) {
            EngineWorkerCoordinator.syncRootMode(
                    getApplicationContext(),
                    intent.getBooleanExtra(EXTRA_ROOT_MODE_ENABLED, false)
            );
            return START_STICKY;
        }

        if (ACTION_PAUSE_SCRIPT.equals(intent.getAction())) {
            requestScriptPause();
            return START_STICKY;
        }

        if (ACTION_RESUME_SCRIPT.equals(intent.getAction())) {
            requestScriptResume();
            return START_STICKY;
        }

        if (ACTION_RESTART_SCRIPT.equals(intent.getAction())) {
            requestScriptRestart();
            return START_STICKY;
        }

        return START_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onDestroy() {
        shutdownRuntime();
        super.onDestroy();
    }

    private void shutdownRuntime() {
        EngineUiHost.closeAll(getApplicationContext());
        EngineWorkerCoordinator.shutdown();
        EngineHttpServer.stop();
    }

    private void runScriptFileInternal(String scriptPath) {
        if (scriptPath == null || scriptPath.isEmpty()) {
            broadcastStatus(STATE_FAILED, "脚本路径为空");
            return;
        }

        ScriptCatalog.ScriptItem item = ScriptCatalog.findSharedFileByPath(this, scriptPath);
        if (item == null) {
            broadcastStatus(STATE_FAILED, "脚本文件不存在或脚本目录不可访问");
            return;
        }
        if (!item.runnable) {
            broadcastStatus(STATE_FAILED, "当前文件不可运行：" + item.fileName);
            return;
        }

        if (!running.compareAndSet(false, true)) {
            broadcastStatus(STATE_RUNNING, "已有脚本正在运行");
            return;
        }

        ScriptCatalog.setSelectedScript(this, item);
        forceStopRequested.set(false);
        activeScriptPath = item.filePath;
        broadcastStatus(STATE_RUNNING, "脚本运行中：" + item.fileName);

        Thread worker = new Thread(() -> {
            String state = STATE_FINISHED;
            String message;
            try {
                JSONObject result;
                if ("alpkg".equals(item.language)) {
                    result = callNativeCommand(
                            "script.runPackage",
                            new JSONObject()
                                    .put("packagePath", item.filePath)
                                    .put("workPath", scriptWorkPath(item.filePath))
                    );
                } else {
                    result = callNativeCommand(
                            "script.run",
                            new JSONObject()
                                    .put("language", item.language)
                                    .put("workPath", scriptWorkPath(item.filePath))
                                    .put("code", ScriptCatalog.readScriptText(item.filePath))
                    );
                }
                message = result.optString("message", "脚本执行完成");
                if (!"finished".equals(result.optString("status", "unknown"))) {
                    state = STATE_FAILED;
                }
            } catch (IOException exception) {
                state = STATE_FAILED;
                message = "读取脚本失败：" + exception.getMessage();
            } catch (JSONException exception) {
                state = STATE_FAILED;
                message = "脚本命令参数错误：" + exception.getMessage();
            } catch (RuntimeException exception) {
                if (forceStopRequested.get()) {
                    state = STATE_FINISHED;
                    message = "已强制停止脚本 Worker";
                } else {
                    state = STATE_FAILED;
                    message = "脚本运行失败：" + exception.getMessage();
                }
            } finally {
                activeScriptPath = null;
                running.set(false);
            }

            broadcastStatus(state, message);
            if (restartRequested.compareAndSet(true, false)) {
                String path = restartScriptPath;
                restartScriptPath = null;
                runScriptFileInternal(path);
            }
        }, "EngineServiceRunSession");
        worker.start();
    }

    private void requestScriptRestart() {
        if (running.get()) {
            String currentPath = activeScriptPath;
            if (currentPath == null || currentPath.isEmpty()) {
                broadcastStatus(STATE_FAILED, "当前脚本路径不可用");
                return;
            }
            restartScriptPath = currentPath;
            restartRequested.set(true);
            requestScriptStop();
            return;
        }

        ScriptCatalog.ScriptItem selected = ScriptCatalog.getSelectedScript(this);
        if (selected == null || !selected.runnable) {
            broadcastStatus(STATE_FAILED, "没有可重启的当前脚本");
            return;
        }
        runScriptFileInternal(selected.filePath);
    }

    private void requestScriptStop() {
        try {
            JSONObject result = callNativeCommand("script.stop", new JSONObject());
            boolean accepted = result.optBoolean("accepted", false);
            String status = result.optString("status", "unknown");

            // 停止命令的实际接受状态由 native 引擎原子判断。这里不能因为用户点击了
            // “停止”就直接把界面改成 stopping，否则空闲时点击会把悬浮按钮错误标绿。
            if (STATE_STOPPING.equals(status)) {
                broadcastStatus(
                        STATE_STOPPING,
                        accepted ? "已请求停止脚本" : "脚本正在停止"
                );
                return;
            }

            broadcastStatus(STATE_FINISHED, "当前没有运行脚本");
        } catch (RuntimeException exception) {
            broadcastStatus(STATE_FAILED, "停止脚本失败：" + exception.getMessage());
        }
    }

    private void requestScriptPause() {
        try {
            JSONObject result = callNativeCommand("script.pause", new JSONObject());
            boolean accepted = result.optBoolean("accepted", false);
            broadcastStatus(
                    accepted ? STATE_PAUSING : STATE_FAILED,
                    accepted ? "已请求暂停脚本" : "当前没有可暂停的脚本"
            );
        } catch (RuntimeException exception) {
            broadcastStatus(STATE_FAILED, "暂停脚本失败：" + exception.getMessage());
        }
    }

    private void requestScriptResume() {
        try {
            JSONObject result = callNativeCommand("script.resume", new JSONObject());
            boolean accepted = result.optBoolean("accepted", false);
            broadcastStatus(
                    accepted ? STATE_RUNNING : STATE_FAILED,
                    accepted ? "已请求继续脚本" : "当前没有已暂停的脚本"
            );
        } catch (RuntimeException exception) {
            broadcastStatus(STATE_FAILED, "继续脚本失败：" + exception.getMessage());
        }
    }

    private static JSONObject callNativeCommand(String method, JSONObject params) {
        try {
            JSONObject envelope = new JSONObject(EngineWorkerCoordinator.callJson(
                    null,
                    method,
                    params == null ? "{}" : params.toString()
            ));
            if (!envelope.optBoolean("ok", false)) {
                throw new IllegalStateException(envelope.optString("error", "原生命令执行失败"));
            }

            JSONObject result = envelope.optJSONObject("result");
            return result == null ? new JSONObject() : result;
        } catch (JSONException exception) {
            throw new IllegalStateException("原生命令 JSON 解析失败：" + method, exception);
        }
    }

    /**
     * 脚本工作目录始终使用实际文件所在目录，不依赖当前进程工作目录。
     *
     * 普通 Lua 和 .alpkg 都通过同一个字段传给 native，m.getWorkPath() 因而在任意运行
     * 入口下都能得到稳定的外部脚本目录。
     */
    private static String scriptWorkPath(String scriptPath) {
        if (scriptPath == null || scriptPath.isEmpty()) {
            return "";
        }
        File file = new File(scriptPath);
        String parent = file.getParent();
        return parent == null ? "" : parent;
    }

    private void broadcastStatus(String state, String message) {
        broadcastStatus(this, state, message);
    }

    private static void broadcastStatus(Context context, String state, String message) {
        Intent intent = new Intent(ACTION_STATUS);
        intent.setPackage(context.getPackageName());
        intent.putExtra(EXTRA_STATE, state);
        intent.putExtra(EXTRA_MESSAGE, message == null ? "" : message);
        context.sendBroadcast(intent);
    }

}
