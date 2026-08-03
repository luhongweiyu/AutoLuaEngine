package com.xiaoyv.engine;

import android.os.ParcelFileDescriptor;
import android.view.Surface;

/** 一次性脚本 Worker 向常驻 :engine 控制进程暴露的进程内核入口。 */
interface IEngineWorker {
    ParcelFileDescriptor callJsonPipe(String method, in ParcelFileDescriptor paramsJson);
    ParcelFileDescriptor openScreenFrame();

    boolean attachImGuiSurface(in Surface surface);
    void detachImGuiSurface();
    void notifyImGuiSurfaceFailure(String message);
    void enqueueImGuiTouch(int action, int pointerId, float x, float y);
    void enqueueImGuiText(String text);
    void enqueueImGuiKey(int action, int keyCode, int unicodeCodePoint, int metaState);
    void enqueueImGuiScroll(float horizontal, float vertical);

    int processId();
    int processUid();
    oneway void shutdown();
}
