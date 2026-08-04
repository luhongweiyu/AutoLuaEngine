package com.xiaoyv.engine;

import android.view.Surface;

/** 主进程 MediaProjection 会话向非 Root Worker 暴露的 Surface 宿主。 */
interface IScreenCaptureHost {
    boolean isReady();
    long attachSurface(in Surface surface, int width, int height, int densityDpi);
    void detachSurface(long leaseId);
}
