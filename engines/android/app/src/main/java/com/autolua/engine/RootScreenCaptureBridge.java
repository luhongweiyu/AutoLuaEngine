/**
 * 文件用途：封装 Root 模式截图入口，只调用常驻 Root helper。
 */
package com.autolua.engine;

import java.nio.ByteBuffer;

/**
 * Root 截图桥。
 *
 * 截图不走其他路线回退，也不为每帧拉起 screencap 外部进程。
 * Root 权限和 helper 进程应在打开引擎或切换 Root 模式时提前准备。
 */
public final class RootScreenCaptureBridge {
    private RootScreenCaptureBridge() {
    }

    public static ScreenCaptureResult captureFrame() {
        return captureFrame(null, 0);
    }

    public static ScreenCaptureResult captureFrame(ByteBuffer targetBuffer, int targetCapacity) {
        if (!RootShellBridge.isRootRuntimeReady()) {
            return ScreenCaptureResult.failure("Root 运行层未就绪");
        }

        return RootHelperBridge.captureFrame(0, 0, targetBuffer, targetCapacity);
    }
}
