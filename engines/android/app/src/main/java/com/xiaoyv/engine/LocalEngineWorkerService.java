/**
 * 文件用途：在非 Root 模式用一次性 :worker 进程承载共用脚本运行时。
 */
package com.xiaoyv.engine;

import android.app.Service;
import android.content.Intent;
import android.os.IBinder;

import java.util.concurrent.atomic.AtomicBoolean;

/** 非 Root 外壳只提供 App UID；内核与 Root Worker 完全相同。 */
public final class LocalEngineWorkerService extends Service {
    private final AtomicBoolean exitScheduled = new AtomicBoolean(false);
    private EngineWorkerEndpoint endpoint;

    @Override
    public void onCreate() {
        super.onCreate();
        endpoint = new EngineWorkerEndpoint(this, null, this::scheduleProcessExit);
    }

    @Override
    public IBinder onBind(Intent intent) {
        return endpoint;
    }

    @Override
    public void onDestroy() {
        if (endpoint != null) {
            endpoint.close();
        }
        scheduleProcessExit();
        super.onDestroy();
    }

    private void scheduleProcessExit() {
        if (!exitScheduled.compareAndSet(false, true)) {
            return;
        }
        stopSelf();
        Thread exitThread = new Thread(() -> {
            try {
                Thread.sleep(80L);
            } catch (InterruptedException exception) {
                Thread.currentThread().interrupt();
            }
            android.os.Process.killProcess(android.os.Process.myPid());
        }, "LocalEngineWorkerExit");
        exitThread.setDaemon(true);
        exitThread.start();
    }
}
