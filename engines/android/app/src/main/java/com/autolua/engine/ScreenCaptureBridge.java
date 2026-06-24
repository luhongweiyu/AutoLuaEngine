package com.autolua.engine;

import android.content.Context;
import android.content.Intent;
import android.graphics.PixelFormat;
import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.media.Image;
import android.media.ImageReader;
import android.media.projection.MediaProjectionManager;
import android.media.projection.MediaProjection;
import android.os.Handler;
import android.os.Looper;
import android.util.DisplayMetrics;
import android.view.WindowManager;

/**
 * 截图授权桥。
 *
 * MediaProjection 需要用户确认授权弹窗，不能静默获取。
 * 当前实现会在授权后创建一条截图会话，并把最新一帧交给 Native 图片句柄。
 */
public final class ScreenCaptureBridge {
    private static final Object LOCK = new Object();
    private static final long CAPTURE_TIMEOUT_MS = 2000;
    private static final long CAPTURE_RETRY_INTERVAL_MS = 50;
    private static final long FOREGROUND_SERVICE_TIMEOUT_MS = 1000;

    private static Context appContext;
    private static int resultCode;
    private static Intent resultData;
    private static MediaProjection mediaProjection;
    private static ImageReader imageReader;
    private static VirtualDisplay virtualDisplay;
    private static int captureWidth;
    private static int captureHeight;
    private static int densityDpi;

    /**
     * MediaProjection 被系统或用户停止后，要清掉当前截图会话。
     *
     * Android 14 对 MediaProjection token 使用更严格，因此会话失效后需要用户
     * 重新点击授权按钮，不能假装还能继续截图。
     */
    private static final MediaProjection.Callback PROJECTION_CALLBACK = new MediaProjection.Callback() {
        @Override
        public void onStop() {
            synchronized (LOCK) {
                releaseSessionLocked(false);
                mediaProjection = null;
                resultData = null;
            }
        }
    };

    private ScreenCaptureBridge() {
    }

    public static void init(Context context) {
        appContext = context.getApplicationContext();
    }

    public static Intent createCaptureIntent(Context context) {
        MediaProjectionManager manager =
                (MediaProjectionManager) context.getSystemService(Context.MEDIA_PROJECTION_SERVICE);
        if (manager == null) {
            return null;
        }
        return manager.createScreenCaptureIntent();
    }

    public static void savePermission(int code, Intent data) {
        synchronized (LOCK) {
            releaseSessionLocked(true);
            resultCode = code;
            resultData = new Intent(data);
        }

        if (appContext != null) {
            ScreenCaptureForegroundService.start(appContext);
        }
    }

    public static boolean hasPermission() {
        synchronized (LOCK) {
            return resultData != null;
        }
    }

    public static int getResultCode() {
        synchronized (LOCK) {
            return resultCode;
        }
    }

    public static Intent getResultData() {
        synchronized (LOCK) {
            return resultData;
        }
    }

    /**
     * 截取当前屏幕并返回紧凑的 RGBA 内存帧。
     *
     * 这里不做 PNG 编码、不写磁盘，避免把后续高频找色建立在低效路径上。
     * Native 层会把像素保存为图片句柄，后续算法直接在内存上扫描。
     */
    public static ScreenCaptureResult captureFrame() {
        ImageReader reader;
        int width;
        int height;

        synchronized (LOCK) {
            String sessionError = ensureSessionLocked();
            if (sessionError != null) {
                return ScreenCaptureResult.failure(sessionError);
            }

            reader = imageReader;
            width = captureWidth;
            height = captureHeight;
        }

        Image image = acquireLatestImage(reader);
        if (image == null) {
            return ScreenCaptureResult.failure("screen capture image is not ready");
        }

        ScreenCaptureResult result = ScreenCaptureResult.successFromImage(image, width, height);
        if (!result.success) {
            image.close();
        }
        return result;
    }

    private static String ensureSessionLocked() {
        if (appContext == null) {
            return "screen capture bridge is not initialized";
        }

        if (resultData == null) {
            return "screen capture permission is not granted";
        }

        ScreenCaptureForegroundService.start(appContext);
        if (!waitForForegroundService()) {
            return "screen capture foreground service is not ready";
        }

        if (mediaProjection != null && imageReader != null && virtualDisplay != null) {
            return null;
        }

        MediaProjectionManager manager =
                (MediaProjectionManager) appContext.getSystemService(Context.MEDIA_PROJECTION_SERVICE);
        if (manager == null) {
            return "screen capture is not available";
        }

        DisplayMetrics metrics = readDisplayMetrics();
        if (metrics.widthPixels <= 0 || metrics.heightPixels <= 0) {
            return "read display metrics failed";
        }

        try {
            mediaProjection = manager.getMediaProjection(resultCode, resultData);
            if (mediaProjection == null) {
                return "create screen capture session failed";
            }

            captureWidth = metrics.widthPixels;
            captureHeight = metrics.heightPixels;
            densityDpi = metrics.densityDpi;

            imageReader = ImageReader.newInstance(
                    captureWidth,
                    captureHeight,
                    PixelFormat.RGBA_8888,
                    2
            );

            // Android 14 要求 createVirtualDisplay 前先注册 callback。
            mediaProjection.registerCallback(PROJECTION_CALLBACK, new Handler(Looper.getMainLooper()));
            virtualDisplay = mediaProjection.createVirtualDisplay(
                    "AutoLuaEngineScreenCapture",
                    captureWidth,
                    captureHeight,
                    densityDpi,
                    DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR,
                    imageReader.getSurface(),
                    null,
                    null
            );

            if (virtualDisplay == null) {
                releaseSessionLocked(true);
                return "create screen capture display failed";
            }
        } catch (RuntimeException exception) {
            releaseSessionLocked(true);
            return "create screen capture session failed: " + exception.getMessage();
        }

        return null;
    }

    private static DisplayMetrics readDisplayMetrics() {
        DisplayMetrics metrics = new DisplayMetrics();
        WindowManager windowManager =
                (WindowManager) appContext.getSystemService(Context.WINDOW_SERVICE);
        if (windowManager != null) {
            windowManager.getDefaultDisplay().getRealMetrics(metrics);
            return metrics;
        }

        DisplayMetrics resourceMetrics = appContext.getResources().getDisplayMetrics();
        metrics.setTo(resourceMetrics);
        return metrics;
    }

    private static Image acquireLatestImage(ImageReader reader) {
        long deadline = System.currentTimeMillis() + CAPTURE_TIMEOUT_MS;
        Image image = null;

        while (System.currentTimeMillis() < deadline) {
            Image latest = reader.acquireLatestImage();
            if (latest != null) {
                if (image != null) {
                    image.close();
                }
                image = latest;
            }

            if (image != null) {
                return image;
            }

            try {
                Thread.sleep(CAPTURE_RETRY_INTERVAL_MS);
            } catch (InterruptedException exception) {
                Thread.currentThread().interrupt();
                return null;
            }
        }

        return image;
    }

    private static boolean waitForForegroundService() {
        long deadline = System.currentTimeMillis() + FOREGROUND_SERVICE_TIMEOUT_MS;

        while (System.currentTimeMillis() < deadline) {
            if (ScreenCaptureForegroundService.isForegroundStarted()) {
                return true;
            }

            try {
                Thread.sleep(CAPTURE_RETRY_INTERVAL_MS);
            } catch (InterruptedException exception) {
                Thread.currentThread().interrupt();
                return false;
            }
        }

        return ScreenCaptureForegroundService.isForegroundStarted();
    }

    private static void releaseSessionLocked(boolean stopProjection) {
        if (virtualDisplay != null) {
            virtualDisplay.release();
            virtualDisplay = null;
        }

        if (imageReader != null) {
            imageReader.close();
            imageReader = null;
        }

        if (stopProjection && mediaProjection != null) {
            try {
                mediaProjection.stop();
            } catch (RuntimeException ignored) {
                // stop 可能与系统回调同时发生。这里清理资源即可，不把它暴露给脚本。
            }
            mediaProjection = null;
        }
    }
}
