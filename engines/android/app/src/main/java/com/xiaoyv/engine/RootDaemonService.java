/**
 * 文件用途：App 主进程的 RootDaemon 生命周期服务，负责在选中 Root 模式时提前授权。
 */
package com.xiaoyv.engine;

import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.os.IBinder;

/**
 * RootDaemon 主进程服务。
 *
 * :engine 可通过 startService 请求它准备或关闭 daemon，但实际 su 调用始终发生在 App 主进程。
 * 这样强停 :engine 后，RootDaemon 仍由主进程和悬浮窗存活，不会随下次脚本运行重新授权。
 */
public final class RootDaemonService extends Service {
    private static final String ACTION_PREPARE = "com.xiaoyv.engine.action.PREPARE_ROOT_DAEMON";
    private static final String ACTION_SHUTDOWN = "com.xiaoyv.engine.action.SHUTDOWN_ROOT_DAEMON";
    private static final String ACTION_SYNC_VOLUME_KEYS =
            "com.xiaoyv.engine.action.SYNC_ROOT_VOLUME_KEYS";

    private RootVolumeKeyMonitor volumeKeyMonitor;

    public static void ensureForCurrentMode(Context context) {
        if (context != null && EngineSettings.isRootModeEnabled(context)) {
            sendAction(context, ACTION_PREPARE);
        }
    }

    public static void setRootModeEnabled(Context context, boolean enabled) {
        if (context == null) {
            return;
        }
        sendAction(context, enabled ? ACTION_PREPARE : ACTION_SHUTDOWN);
    }

    /**
     * 设置页切换音量键控制后，立即同步 Root 监听连接。
     */
    public static void syncVolumeKeyControl(Context context) {
        if (context != null) {
            sendAction(context, ACTION_SYNC_VOLUME_KEYS);
        }
    }

    private static void sendAction(Context context, String action) {
        Intent intent = new Intent(context, RootDaemonService.class);
        intent.setAction(action);
        context.startService(intent);
    }

    @Override
    public void onCreate() {
        super.onCreate();
        volumeKeyMonitor = new RootVolumeKeyMonitor(getApplicationContext());
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String action = intent == null ? ACTION_PREPARE : intent.getAction();
        if (ACTION_SHUTDOWN.equals(action)) {
            volumeKeyMonitor.stop();
            new Thread(() -> {
                RootDaemonManager.shutdown(getApplicationContext());
                broadcastState(EngineService.STATE_FINISHED, "RootDaemon 已关闭");
                stopSelf(startId);
            }, "RootDaemonShutdown").start();
            return START_NOT_STICKY;
        }

        if (ACTION_SYNC_VOLUME_KEYS.equals(action)) {
            if (!EngineSettings.isRootModeEnabled(this)
                    || !EngineSettings.isVolumeKeyControlEnabled(this)) {
                volumeKeyMonitor.stop();
                return START_STICKY;
            }

            new Thread(() -> {
                boolean ready = RootDaemonManager.prepare(getApplicationContext());
                if (ready
                        && EngineSettings.isRootModeEnabled(this)
                        && EngineSettings.isVolumeKeyControlEnabled(this)) {
                    volumeKeyMonitor.start();
                } else {
                    volumeKeyMonitor.stop();
                }
                // 这里只同步控制入口，不广播 finished，避免脚本运行中切换设置时误改 UI 状态。
            }, "RootVolumeKeySync").start();
            return START_STICKY;
        }

        new Thread(() -> {
            boolean ready = RootDaemonManager.prepare(getApplicationContext());
            if (ready
                    && EngineSettings.isRootModeEnabled(this)
                    && EngineSettings.isVolumeKeyControlEnabled(this)) {
                volumeKeyMonitor.start();
            } else {
                volumeKeyMonitor.stop();
            }
            broadcastState(
                    ready ? EngineService.STATE_FINISHED : EngineService.STATE_FAILED,
                    ready
                            ? "Root 运行层已就绪"
                            : "Root 初始化失败：" + RootDaemonManager.lastError()
            );
        }, "RootDaemonPrepare").start();
        return START_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onDestroy() {
        if (volumeKeyMonitor != null) {
            volumeKeyMonitor.stop();
        }
        super.onDestroy();
    }

    private void broadcastState(String state, String message) {
        Intent intent = new Intent(EngineService.ACTION_STATUS);
        intent.setPackage(getPackageName());
        intent.putExtra(EngineService.EXTRA_STATE, state);
        intent.putExtra(EngineService.EXTRA_MESSAGE, message);
        sendBroadcast(intent);
    }
}
