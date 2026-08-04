/**
 * 文件用途：封装 Root 模式截图入口，在 uid=0 Worker 内直接读取屏幕。
 */
package com.xiaoyv.engine;

import android.os.Process;

import java.nio.ByteBuffer;

/**
 * Root 截图桥。
 *
 * 截图不走其他路线回退，也不经过 RootDaemon 传输整帧点阵。
 */
public final class RootScreenCaptureBridge {
    private RootScreenCaptureBridge() {
    }

    public static ScreenCaptureResult captureFrame() {
        return captureFrame(null, 0);
    }

    public static ScreenCaptureResult captureFrame(ByteBuffer targetBuffer, int targetCapacity) {
        if (Process.myUid() != 0) {
            return ScreenCaptureResult.failure("Root 截图只能在 uid=0 Worker 中执行");
        }

        return SurfaceScreenCaptureBridge.captureFrame(targetBuffer, targetCapacity);
    }
}
