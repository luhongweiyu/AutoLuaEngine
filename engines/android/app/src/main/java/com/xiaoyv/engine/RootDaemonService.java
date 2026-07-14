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
     * 设置页切换音量键控制后，只同步现有 RootDaemon 的监听连接。
     *
     * Root 授权只能由 App 启动或用户切换到 Root 模式时触发。音量键开关只是一个控制入口，
     * 不得调用 {@link RootDaemonManager#prepare(Context)}：否则 RootDaemon 已退出时，用户每次
     * 切换开关都会意外执行 {@code su} 并弹出超级用户授权框。
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
        String action = intent == null ? null : intent.getAction();

        /*
         * START_STICKY 服务被系统重建时会收到 null Intent。它只应该重新挂接已有 RootDaemon
         * 的音量键监听，绝不能把“系统重建”误判为“用户要求启动 Root 模式”并执行 su。
         */
        if (action == null) {
            syncExistingVolumeKeyMonitor();
            return START_STICKY;
        }

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
            syncExistingVolumeKeyMonitor();
            return START_STICKY;
        }

        // 未知 Action 不具备 Root 初始化语义，直接忽略，避免任何意外入口触发 su。
        if (!ACTION_PREPARE.equals(action)) {
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

            // Root 正常就绪是 App 启动或模式切换后的预期状态，不占用脚本状态提示。
            // 状态页会通过 device.info 实时展示 Root 是否可用；只有初始化失败才需要主动通知。
            if (!ready) {
                broadcastState(
                        EngineService.STATE_FAILED,
                        "Root 初始化失败：" + RootDaemonManager.lastError()
                );
            }
        }, "RootDaemonPrepare").start();
        return START_STICKY;
    }

    /**
     * 按当前开关状态接入或关闭音量键监听。
     *
     * 此方法没有 Root 探测和 daemon 启动逻辑。已存在的 RootDaemon 会立即接受订阅；不存在时
     * 监听线程自行结束，用户下次显式打开 App 或切换 Root 模式才会进入授权流程。
     */
    private void syncExistingVolumeKeyMonitor() {
        if (EngineSettings.isRootModeEnabled(this)
                && EngineSettings.isVolumeKeyControlEnabled(this)) {
            volumeKeyMonitor.start();
        } else {
            volumeKeyMonitor.stop();
        }
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
