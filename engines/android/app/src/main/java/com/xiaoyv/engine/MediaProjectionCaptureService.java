/**
 * 文件用途：在 App 主进程持有非 Root 屏幕录制授权和唯一 VirtualDisplay。
 */
package com.xiaoyv.engine;

import android.app.Activity;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.media.projection.MediaProjection;
import android.media.projection.MediaProjectionManager;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.RemoteException;
import android.view.Surface;

/**
 * MediaProjection 授权会话宿主。
 *
 * Android 14 的授权令牌只允许创建一个 VirtualDisplay，因此 Worker 更换时只替换 Surface，
 * 不释放并重建 VirtualDisplay。投屏会话随本 Service 或系统授权结束而释放。
 */
public final class MediaProjectionCaptureService extends Service {
    private static final String ACTION_START =
            "com.xiaoyv.engine.action.START_MEDIA_PROJECTION";
    private static final String EXTRA_RESULT_CODE = "resultCode";
    private static final String EXTRA_RESULT_DATA = "resultData";
    private static final String EXTRA_PENDING_SCRIPT_PATH = "pendingScriptPath";
    private static final String CHANNEL_ID = "screen_capture";
    private static final int NOTIFICATION_ID = 1042;

    private static volatile boolean projectionReady;

    private final Object sessionLock = new Object();
    private MediaProjection mediaProjection;
    private VirtualDisplay virtualDisplay;
    private Surface attachedSurface;
    private long attachedSurfaceLease;
    private long nextSurfaceLease = 1L;
    private boolean stopping;

    private final MediaProjection.Callback projectionCallback = new MediaProjection.Callback() {
        @Override
        public void onStop() {
            stopProjection(false);
            stopSelf();
        }
    };

    private final IScreenCaptureHost.Stub binder = new IScreenCaptureHost.Stub() {
        @Override
        public boolean isReady() {
            return projectionReady;
        }

        @Override
        public long attachSurface(
                Surface surface,
                int width,
                int height,
                int densityDpi
        ) throws RemoteException {
            if (surface == null || !surface.isValid() || width <= 0 || height <= 0) {
                return 0L;
            }
            synchronized (sessionLock) {
                if (mediaProjection == null || stopping) {
                    return 0L;
                }
                try {
                    replaceAttachedSurfaceLocked(surface);
                    if (virtualDisplay == null) {
                        virtualDisplay = mediaProjection.createVirtualDisplay(
                                "XiaoyvNonRootCapture",
                                width,
                                height,
                                Math.max(1, densityDpi),
                                DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR,
                                attachedSurface,
                                null,
                                null
                        );
                    } else {
                        virtualDisplay.resize(width, height, Math.max(1, densityDpi));
                        virtualDisplay.setSurface(attachedSurface);
                    }
                    if (virtualDisplay == null) {
                        detachSurfaceLocked();
                        return 0L;
                    }
                    if (nextSurfaceLease <= 0L) {
                        nextSurfaceLease = 1L;
                    }
                    attachedSurfaceLease = nextSurfaceLease++;
                    return attachedSurfaceLease;
                } catch (RuntimeException exception) {
                    detachSurfaceLocked();
                    return 0L;
                }
            }
        }

        @Override
        public void detachSurface(long leaseId) {
            synchronized (sessionLock) {
                if (leaseId != 0L && leaseId == attachedSurfaceLease) {
                    detachSurfaceLocked();
                }
            }
        }
    };

    public static boolean isProjectionReady() {
        return projectionReady;
    }

    public static void start(Context context, int resultCode, Intent resultData) {
        start(context, resultCode, resultData, null);
    }

    public static void start(
            Context context,
            int resultCode,
            Intent resultData,
            String pendingScriptPath
    ) {
        if (context == null || resultCode != Activity.RESULT_OK || resultData == null) {
            return;
        }
        Intent intent = new Intent(context, MediaProjectionCaptureService.class);
        intent.setAction(ACTION_START);
        intent.putExtra(EXTRA_RESULT_CODE, resultCode);
        intent.putExtra(EXTRA_RESULT_DATA, resultData);
        if (pendingScriptPath != null && !pendingScriptPath.isEmpty()) {
            intent.putExtra(EXTRA_PENDING_SCRIPT_PATH, pendingScriptPath);
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            context.startForegroundService(intent);
        } else {
            context.startService(intent);
        }
    }

    public static void stop(Context context) {
        if (context == null) {
            return;
        }
        context.stopService(new Intent(context, MediaProjectionCaptureService.class));
    }

    @Override
    public void onCreate() {
        super.onCreate();
        createNotificationChannel();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String action = intent == null ? null : intent.getAction();
        if (!ACTION_START.equals(action)) {
            return START_NOT_STICKY;
        }

        startForegroundSession();
        int resultCode = intent.getIntExtra(EXTRA_RESULT_CODE, Activity.RESULT_CANCELED);
        Intent resultData = intent.getParcelableExtra(EXTRA_RESULT_DATA);
        String pendingScriptPath = intent.getStringExtra(EXTRA_PENDING_SCRIPT_PATH);
        if (resultCode != Activity.RESULT_OK || resultData == null) {
            stopProjection(true);
            stopSelf(startId);
            return START_NOT_STICKY;
        }

        boolean ready;
        synchronized (sessionLock) {
            if (mediaProjection != null && !stopping) {
                // 同一授权会话可能在系统回调或用户重复点击后再次收到启动 Intent。
                // 不能提前返回，否则这个 Intent 携带的待运行脚本会被吞掉。
                projectionReady = true;
                ready = true;
            } else {
                stopping = false;
                projectionReady = false;
                MediaProjectionManager manager = (MediaProjectionManager) getSystemService(
                        Context.MEDIA_PROJECTION_SERVICE
                );
                mediaProjection = manager == null
                        ? null
                        : manager.getMediaProjection(resultCode, resultData);
                if (mediaProjection != null) {
                    mediaProjection.registerCallback(
                            projectionCallback,
                            new Handler(Looper.getMainLooper())
                    );
                }
                ready = mediaProjection != null;
                projectionReady = ready;
            }
        }
        if (!ready) {
            stopProjection(true);
            stopSelf(startId);
        } else if (pendingScriptPath != null && !pendingScriptPath.isEmpty()) {
            EngineService.runScriptFile(this, pendingScriptPath);
        }
        return START_NOT_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return binder;
    }

    @Override
    public void onDestroy() {
        stopProjection(true);
        super.onDestroy();
    }

    private void startForegroundSession() {
        Intent activityIntent = new Intent(this, MainActivity.class);
        PendingIntent pendingIntent = PendingIntent.getActivity(
                this,
                0,
                activityIntent,
                Build.VERSION.SDK_INT >= Build.VERSION_CODES.M
                        ? PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT
                        : PendingIntent.FLAG_UPDATE_CURRENT
        );
        Notification.Builder builder = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                ? new Notification.Builder(this, CHANNEL_ID)
                : new Notification.Builder(this);
        Notification notification = builder
                .setSmallIcon(R.drawable.ic_xiaoyv_launcher)
                .setContentTitle("小鱼精灵")
                .setContentText("非 Root 屏幕读取已启用")
                .setContentIntent(pendingIntent)
                .setOngoing(true)
                .build();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(
                    NOTIFICATION_ID,
                    notification,
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION
            );
        } else {
            startForeground(NOTIFICATION_ID, notification);
        }
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) {
            return;
        }
        NotificationManager manager = (NotificationManager) getSystemService(
                Context.NOTIFICATION_SERVICE
        );
        if (manager == null) {
            return;
        }
        NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID,
                "屏幕读取",
                NotificationManager.IMPORTANCE_LOW
        );
        channel.setDescription("非 Root 模式读取屏幕时显示");
        manager.createNotificationChannel(channel);
    }

    private void replaceAttachedSurfaceLocked(Surface surface) {
        if (attachedSurface == surface) {
            return;
        }
        if (attachedSurface != null) {
            attachedSurface.release();
        }
        attachedSurface = surface;
    }

    private void detachSurfaceLocked() {
        if (virtualDisplay != null) {
            try {
                virtualDisplay.setSurface(null);
            } catch (RuntimeException ignored) {
                // 投屏会话可能正在由系统关闭。
            }
        }
        if (attachedSurface != null) {
            attachedSurface.release();
            attachedSurface = null;
        }
        attachedSurfaceLease = 0L;
    }

    private void stopProjection(boolean requestStop) {
        MediaProjection projectionToStop;
        synchronized (sessionLock) {
            if (stopping) {
                return;
            }
            stopping = true;
            projectionReady = false;
            detachSurfaceLocked();
            if (virtualDisplay != null) {
                virtualDisplay.release();
                virtualDisplay = null;
            }
            projectionToStop = mediaProjection;
            mediaProjection = null;
        }
        if (projectionToStop != null) {
            projectionToStop.unregisterCallback(projectionCallback);
            if (requestStop) {
                projectionToStop.stop();
            }
        }
        stopForeground(true);
    }
}
