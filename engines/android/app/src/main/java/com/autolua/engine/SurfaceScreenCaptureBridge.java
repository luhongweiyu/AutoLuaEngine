package com.autolua.engine;

import android.graphics.Bitmap;
import android.graphics.Rect;
import android.content.Context;
import android.os.Build;
import android.util.DisplayMetrics;
import android.view.WindowManager;

import java.lang.reflect.Constructor;
import java.lang.reflect.Method;
import java.nio.ByteBuffer;

/**
 * 基于系统 Surface/Window 隐藏 API 的高速截图桥。
 *
 * 这条路线参考旧项目的 NativeService 截图实现：优先直接请求系统当前显示内容，
 * 避免每帧执行 `su screencap` 带来的进程启动、stdout 大块传输和原始数据解析开销。
 *
 * 注意：这些都是 Android 隐藏 API，不同系统可能限制普通 App 进程调用。第一版把
 * 它作为 root 模式的首选高频路线；如果当前设备拒绝访问，后续会把同样的调用下沉到
 * root helper/app_process 常驻进程中执行。
 */
public final class SurfaceScreenCaptureBridge {
    private static final String SOURCE_SURFACE_CONTROL = "root-surface";
    private static final String SOURCE_SCREEN_CAPTURE = "root-screen-capture";

    private SurfaceScreenCaptureBridge() {
    }

    public static ScreenCaptureResult captureFrame() {
        long startTime = System.nanoTime();
        DisplayMetrics metrics = readDisplayMetrics();
        if (metrics.widthPixels <= 0 || metrics.heightPixels <= 0) {
            return ScreenCaptureResult.failure("读取屏幕尺寸失败");
        }

        try {
            Bitmap bitmap = captureBitmap(metrics.widthPixels, metrics.heightPixels);
            if (bitmap != null) {
                return bitmapToResult(bitmap, SOURCE_SURFACE_CONTROL, elapsedMillis(startTime));
            }
        } catch (ReflectiveOperationException | RuntimeException exception) {
            return ScreenCaptureResult.failure("SurfaceControl 截图失败：" + exception.getMessage());
        }

        try {
            Bitmap bitmap = Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE
                    ? captureByScreenCapture()
                    : null;
            if (bitmap != null) {
                return bitmapToResult(bitmap, SOURCE_SCREEN_CAPTURE, elapsedMillis(startTime));
            }
        } catch (ReflectiveOperationException | RuntimeException exception) {
            return ScreenCaptureResult.failure("ScreenCapture 截图失败：" + exception.getMessage());
        }

        return ScreenCaptureResult.failure("当前系统隐藏截图接口不可用");
    }

    static Bitmap captureBitmapForRootHelper(int width, int height)
            throws ReflectiveOperationException {
        return captureBitmap(width, height);
    }

    private static Bitmap captureBitmap(int width, int height)
            throws ReflectiveOperationException {
        return Build.VERSION.SDK_INT >= Build.VERSION_CODES.S
                ? captureBySurfaceControl(width, height)
                : captureByLegacySurfaceControl(width, height);
    }

    private static Bitmap captureByLegacySurfaceControl(int width, int height)
            throws ReflectiveOperationException {
        Class<?> surfaceControlClass = Class.forName("android.view.SurfaceControl");
        Object displayToken = getLegacyDisplayToken(surfaceControlClass);
        if (displayToken != null) {
            Bitmap bitmap = tryLegacyScreenshotWithDisplayToken(
                    surfaceControlClass,
                    displayToken,
                    width,
                    height
            );
            if (bitmap != null) {
                return bitmap;
            }
        }

        Bitmap bitmap = tryLegacyScreenshotWithRect(surfaceControlClass, width, height);
        if (bitmap != null) {
            return bitmap;
        }

        return tryLegacyScreenshotWithSize(surfaceControlClass, width, height);
    }

    private static Object getLegacyDisplayToken(Class<?> surfaceControlClass)
            throws ReflectiveOperationException {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            Method method = surfaceControlClass.getDeclaredMethod("getInternalDisplayToken");
            method.setAccessible(true);
            return method.invoke(null);
        }

        Method method = surfaceControlClass.getDeclaredMethod("getBuiltInDisplay", int.class);
        method.setAccessible(true);
        return method.invoke(null, 0);
    }

    private static Bitmap tryLegacyScreenshotWithDisplayToken(
            Class<?> surfaceControlClass,
            Object displayToken,
            int width,
            int height
    ) {
        try {
            Method method = surfaceControlClass.getDeclaredMethod(
                    "screenshot",
                    Class.forName("android.os.IBinder"),
                    Rect.class,
                    int.class,
                    int.class,
                    int.class,
                    int.class,
                    boolean.class,
                    boolean.class,
                    int.class
            );
            method.setAccessible(true);
            return (Bitmap) method.invoke(
                    null,
                    displayToken,
                    new Rect(),
                    width,
                    height,
                    0,
                    0,
                    true,
                    false,
                    0
            );
        } catch (ReflectiveOperationException | RuntimeException exception) {
            return null;
        }
    }

    private static Bitmap tryLegacyScreenshotWithRect(
            Class<?> surfaceControlClass,
            int width,
            int height
    ) {
        try {
            Method method = surfaceControlClass.getDeclaredMethod(
                    "screenshot",
                    Rect.class,
                    int.class,
                    int.class,
                    int.class
            );
            method.setAccessible(true);
            return (Bitmap) method.invoke(null, new Rect(), width, height, 0);
        } catch (ReflectiveOperationException | RuntimeException exception) {
            return null;
        }
    }

    private static Bitmap tryLegacyScreenshotWithSize(
            Class<?> surfaceControlClass,
            int width,
            int height
    ) {
        try {
            Method method = surfaceControlClass.getDeclaredMethod(
                    "screenshot",
                    int.class,
                    int.class
            );
            method.setAccessible(true);
            return (Bitmap) method.invoke(null, width, height);
        } catch (ReflectiveOperationException | RuntimeException exception) {
            return null;
        }
    }

    private static Bitmap captureBySurfaceControl(int width, int height)
            throws ReflectiveOperationException {
        Class<?> surfaceControlClass = Class.forName("android.view.SurfaceControl");
        Method getInternalDisplayToken = surfaceControlClass.getDeclaredMethod("getInternalDisplayToken");
        Object displayToken = getInternalDisplayToken.invoke(null);
        if (displayToken == null) {
            return null;
        }

        Class<?> builderClass = Class.forName(
                "android.view.SurfaceControl$DisplayCaptureArgs$Builder"
        );
        Constructor<?> builderConstructor = builderClass.getConstructor(
                Class.forName("android.os.IBinder")
        );
        Object builder = builderConstructor.newInstance(displayToken);
        builderClass.getMethod("setSourceCrop", Rect.class).invoke(builder, new Rect());
        builderClass.getMethod("setSize", int.class, int.class).invoke(builder, width, height);
        builderClass.getMethod("setUseIdentityTransform", boolean.class).invoke(builder, true);

        Object captureArgs = builderClass.getMethod("build").invoke(builder);
        Method captureDisplay = surfaceControlClass.getDeclaredMethod(
                "captureDisplay",
                Class.forName("android.view.SurfaceControl$DisplayCaptureArgs")
        );
        Object hardwareBuffer = captureDisplay.invoke(null, captureArgs);
        return screenshotHardwareBufferToBitmap(hardwareBuffer);
    }

    private static Bitmap captureByScreenCapture() throws ReflectiveOperationException {
        Class<?> screenCaptureClass = Class.forName("android.window.ScreenCapture");
        Object listener = screenCaptureClass
                .getDeclaredMethod("createSyncCaptureListener")
                .invoke(null);
        if (listener == null) {
            return null;
        }

        Object windowManagerService = Class.forName("android.view.WindowManagerGlobal")
                .getDeclaredMethod("getWindowManagerService")
                .invoke(null);
        Method captureDisplay = windowManagerService.getClass().getMethod(
                "captureDisplay",
                int.class,
                Class.forName("android.window.ScreenCapture$CaptureArgs"),
                listener.getClass().getInterfaces()[0]
        );
        captureDisplay.invoke(windowManagerService, 0, null, listener);
        Object hardwareBuffer = listener.getClass().getMethod("getBuffer").invoke(listener);
        return screenshotHardwareBufferToBitmap(hardwareBuffer);
    }

    private static Bitmap screenshotHardwareBufferToBitmap(Object hardwareBuffer)
            throws ReflectiveOperationException {
        if (hardwareBuffer == null) {
            return null;
        }

        Bitmap bitmap = (Bitmap) hardwareBuffer.getClass().getMethod("asBitmap").invoke(hardwareBuffer);
        closeHardwareBufferIfPresent(hardwareBuffer);
        if (bitmap == null) {
            return null;
        }
        bitmap.setHasAlpha(false);
        if (Bitmap.Config.ARGB_8888.equals(bitmap.getConfig())) {
            return bitmap;
        }
        Bitmap copy = bitmap.copy(Bitmap.Config.ARGB_8888, false);
        bitmap.recycle();
        return copy;
    }

    private static void closeHardwareBufferIfPresent(Object screenshotHardwareBuffer)
            throws ReflectiveOperationException {
        try {
            Object buffer = screenshotHardwareBuffer.getClass()
                    .getMethod("getHardwareBuffer")
                    .invoke(screenshotHardwareBuffer);
            if (buffer != null) {
                buffer.getClass().getMethod("close").invoke(buffer);
            }
        } catch (NoSuchMethodException ignored) {
            // Android 14 的 android.window.ScreenCapture.ScreenshotHardwareBuffer 没有公开同名关闭入口。
        }
    }

    private static ScreenCaptureResult bitmapToResult(
            Bitmap bitmap,
            String source,
            long captureDurationMs
    ) {
        int width = bitmap.getWidth();
        int height = bitmap.getHeight();
        long pixelBytesLong = (long) width * (long) height * 4L;
        if (pixelBytesLong > Integer.MAX_VALUE) {
            bitmap.recycle();
            return ScreenCaptureResult.failure("截图尺寸过大");
        }

        ByteBuffer buffer = ByteBuffer.allocateDirect((int) pixelBytesLong);
        bitmap.copyPixelsToBuffer(buffer);
        buffer.position(0);
        bitmap.recycle();
        return ScreenCaptureResult.successFromRgbaBuffer(
                buffer,
                width,
                height,
                source,
                captureDurationMs
        );
    }

    private static DisplayMetrics readDisplayMetrics() {
        DisplayMetrics metrics = new DisplayMetrics();
        if (AndroidHostBridge.appContext() == null) {
            return metrics;
        }

        WindowManager windowManager =
                (WindowManager) AndroidHostBridge.appContext().getSystemService(Context.WINDOW_SERVICE);
        if (windowManager != null) {
            windowManager.getDefaultDisplay().getRealMetrics(metrics);
        } else {
            metrics.setTo(AndroidHostBridge.appContext().getResources().getDisplayMetrics());
        }
        return metrics;
    }

    private static long elapsedMillis(long startTime) {
        long elapsedNanos = System.nanoTime() - startTime;
        return Math.max(0L, elapsedNanos / 1_000_000L);
    }
}
