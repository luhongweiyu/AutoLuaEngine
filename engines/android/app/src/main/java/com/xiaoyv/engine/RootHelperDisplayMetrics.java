/**
 * 文件用途：Root helper 内部使用的屏幕参数数据结构。
 */
package com.xiaoyv.engine;

import android.graphics.Point;
import android.util.DisplayMetrics;

import java.lang.reflect.Method;

/**
 * root helper 进程读取显示尺寸的小工具。
 *
 * helper 不是 Android Service，没有 Context，不能通过 WindowManager 获取屏幕尺寸。
 * 这里通过 SurfaceControl 的 display token 读取 display configs；失败时返回空 metrics，
 * 调用方会把错误暴露给脚本。
 */
final class RootHelperDisplayMetrics {
    private RootHelperDisplayMetrics() {
    }

    static DisplayMetrics read() {
        DisplayMetrics metrics = new DisplayMetrics();
        try {
            Point size = readDisplaySizeBySurfaceControl();
            metrics.widthPixels = size.x;
            metrics.heightPixels = size.y;
        } catch (ReflectiveOperationException | RuntimeException ignored) {
            metrics.widthPixels = 0;
            metrics.heightPixels = 0;
        }
        return metrics;
    }

    private static Point readDisplaySizeBySurfaceControl() throws ReflectiveOperationException {
        Class<?> surfaceControlClass = Class.forName("android.view.SurfaceControl");
        Object displayToken = getDisplayToken(surfaceControlClass);
        if (displayToken == null) {
            return new Point(0, 0);
        }

        Object config = surfaceControlClass
                .getDeclaredMethod("getActiveConfig", Class.forName("android.os.IBinder"))
                .invoke(null, displayToken);
        if (config == null) {
            return new Point(0, 0);
        }

        Method getWidth = config.getClass().getDeclaredMethod("getWidth");
        Method getHeight = config.getClass().getDeclaredMethod("getHeight");
        return new Point(
                ((Number) getWidth.invoke(config)).intValue(),
                ((Number) getHeight.invoke(config)).intValue()
        );
    }

    private static Object getDisplayToken(Class<?> surfaceControlClass)
            throws ReflectiveOperationException {
        try {
            Method method = surfaceControlClass.getDeclaredMethod("getInternalDisplayToken");
            method.setAccessible(true);
            return method.invoke(null);
        } catch (NoSuchMethodException ignored) {
            Method method = surfaceControlClass.getDeclaredMethod("getBuiltInDisplay", int.class);
            method.setAccessible(true);
            return method.invoke(null, 0);
        }
    }
}
