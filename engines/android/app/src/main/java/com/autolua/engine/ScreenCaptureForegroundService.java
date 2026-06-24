package com.autolua.engine;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.IBinder;

/**
 * MediaProjection 前台服务。
 *
 * Android 10 开始要求录屏类能力声明前台服务类型，Android 14 对这个要求更严格。
 * 截图授权仍然由用户在系统弹窗中确认，本服务只负责让授权后的投屏会话合法运行。
 */
public final class ScreenCaptureForegroundService extends Service {
    private static final String CHANNEL_ID = "screen_capture";
    private static final int NOTIFICATION_ID = 1002;

    private static volatile boolean foregroundStarted;

    public static void start(Context context) {
        Intent intent = new Intent(context, ScreenCaptureForegroundService.class);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            context.startForegroundService(intent);
        } else {
            context.startService(intent);
        }
    }

    public static boolean isForegroundStarted() {
        return foregroundStarted;
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        startInForeground();
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        foregroundStarted = false;
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    private void startInForeground() {
        createNotificationChannel();
        Notification notification = buildNotification();

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(
                    NOTIFICATION_ID,
                    notification,
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION
            );
        } else {
            startForeground(NOTIFICATION_ID, notification);
        }

        foregroundStarted = true;
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) {
            return;
        }

        NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID,
                "Screen capture",
                NotificationManager.IMPORTANCE_LOW
        );
        NotificationManager manager = getSystemService(NotificationManager.class);
        if (manager != null) {
            manager.createNotificationChannel(channel);
        }
    }

    private Notification buildNotification() {
        Notification.Builder builder;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            builder = new Notification.Builder(this, CHANNEL_ID);
        } else {
            builder = new Notification.Builder(this);
        }

        return builder
                .setSmallIcon(android.R.drawable.ic_menu_camera)
                .setContentTitle("AutoLuaEngine")
                .setContentText("Screen capture is running")
                .setOngoing(true)
                .build();
    }
}
