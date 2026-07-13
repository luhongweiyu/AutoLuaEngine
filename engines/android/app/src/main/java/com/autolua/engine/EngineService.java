/**
 * 文件用途：:engine 独立进程脚本服务，负责脚本任务、HTTP 服务和状态广播。
 */
package com.autolua.engine;

import android.app.ActivityManager;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.os.IBinder;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.IOException;
import java.util.List;
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
    public static final String ACTION_FORCE_STOP_ENGINE_PROCESS =
            "com.autolua.engine.action.FORCE_STOP_ENGINE_PROCESS";
    public static final String ACTION_STATUS =
            "com.autolua.engine.action.STATUS";

    public static final String EXTRA_SCRIPT_PATH = "scriptPath";
    public static final String EXTRA_ENABLED = "enabled";
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

    /**
     * 请求引擎运行共享脚本目录中的真实文件路径。
     */
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

    public static void forceStopEngineProcess(Context context) {
        // 强制停止进程是脚本卡死时使用的硬控制：主进程先让 UI 立即回到
        // 未运行状态，再直接结束 :engine 进程，不等待脚本或 HTTP 请求收尾。
        broadcastStatus(context, STATE_FINISHED, "已强制停止引擎进程");
        context.stopService(new Intent(context, EngineService.class));
        killEngineProcessNow(context);
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
            broadcastStatus(STATE_FINISHED, "已强制停止引擎进程");
            shutdownRuntime();
            stopSelf();
            android.os.Process.killProcess(android.os.Process.myPid());
            return START_NOT_STICKY;
        }

        if (ACTION_PAUSE_SCRIPT.equals(intent.getAction())) {
            requestScriptPause();
            return START_STICKY;
        }

        if (ACTION_RESUME_SCRIPT.equals(intent.getAction())) {
            requestScriptResume();
            return START_STICKY;
        }

        if (ACTION_SET_ROOT_MODE.equals(intent.getAction())) {
            setRootModeFromNative(intent.getBooleanExtra(EXTRA_ENABLED, true));
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
        AndroidHostBridge.closeAllScriptUi();
        EngineHttpServer.stop();
        RootHelperBridge.shutdown();
    }

    private void runScriptFileInternal(String scriptPath) {
        if (scriptPath == null || scriptPath.isEmpty()) {
            broadcastStatus(STATE_FAILED, "脚本路径为空");
            return;
        }

        ScriptCatalog.ScriptItem item = ScriptCatalog.findByPath(this, scriptPath);
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
        broadcastStatus(STATE_RUNNING, "脚本运行中：" + item.fileName);

        Thread worker = new Thread(() -> {
            String state = STATE_FINISHED;
            String message;
            try {
                JSONObject result;
                if ("alpkg".equals(item.language)) {
                    result = callNativeCommand(
                            "script.runPackage",
                            new JSONObject().put("packagePath", item.filePath)
                    );
                } else {
                    result = callNativeCommand(
                            "script.run",
                            new JSONObject()
                                    .put("language", item.language)
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
                state = STATE_FAILED;
                message = "脚本运行失败：" + exception.getMessage();
            } finally {
                running.set(false);
            }

            broadcastStatus(state, message);
        }, "EngineServiceLuaWorker");
        worker.start();
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

    private void setRootModeFromNative(boolean enabled) {
        try {
            JSONObject result = callNativeCommand(
                    "device.setRootModeEnabled",
                    new JSONObject().put("enabled", enabled)
            );
            if (!enabled) {
                broadcastStatus(STATE_FINISHED, "运行模式已切换为无障碍优先");
                return;
            }

            String message = result.optBoolean("rootAvailable", false)
                    ? "运行模式已切换为 Root 优先，Root 运行层已就绪"
                    : "运行模式已切换为 Root 优先，但 Root 权限不可用";
            broadcastStatus(
                    result.optBoolean("rootAvailable", false) ? STATE_FINISHED : STATE_FAILED,
                    message
            );
        } catch (JSONException exception) {
            broadcastStatus(STATE_FAILED, "Root 模式参数错误：" + exception.getMessage());
        } catch (RuntimeException exception) {
            broadcastStatus(STATE_FAILED, "Root 模式切换失败：" + exception.getMessage());
        }
    }

    private static JSONObject callNativeCommand(String method, JSONObject params) {
        try {
            JSONObject envelope = new JSONObject(NativeEngine.callJson(
                    method,
                    params == null ? "{}" : params.toString()
            ));
            if (!envelope.optBoolean("ok", false)) {
                throw new IllegalStateException(envelope.optString("error", "native command failed"));
            }

            JSONObject result = envelope.optJSONObject("result");
            return result == null ? new JSONObject() : result;
        } catch (JSONException exception) {
            throw new IllegalStateException("native command json parse failed: " + method, exception);
        }
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

    private static void killEngineProcessNow(Context context) {
        ActivityManager activityManager =
                (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
        if (activityManager == null) {
            return;
        }

        List<ActivityManager.RunningAppProcessInfo> processes =
                activityManager.getRunningAppProcesses();
        if (processes == null) {
            return;
        }

        String engineProcessName = context.getPackageName() + ":engine";
        for (ActivityManager.RunningAppProcessInfo process : processes) {
            if (engineProcessName.equals(process.processName)) {
                android.os.Process.killProcess(process.pid);
            }
        }
    }
}
