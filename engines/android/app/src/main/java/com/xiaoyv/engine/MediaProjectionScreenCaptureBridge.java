/**
 * 文件用途：在非 Root Worker 内接收 MediaProjection 最新帧并写入 native 截图缓冲。
 */
package com.xiaoyv.engine;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.media.Image;
import android.media.ImageReader;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.RemoteException;
import android.util.DisplayMetrics;
import android.view.Surface;
import android.view.WindowManager;

import java.nio.ByteBuffer;

/** 非 Root 截图只存在于 App UID 的一次性 Worker，不向 Root Worker 暴露。 */
public final class MediaProjectionScreenCaptureBridge {
    private static final long HOST_CONNECT_TIMEOUT_MS = 1500L;
    private static final long FIRST_FRAME_TIMEOUT_MS = 3000L;
    private static final Object STATE_LOCK = new Object();
    private static final Object CAPTURE_LOCK = new Object();
    private static final Object FRAME_LOCK = new Object();

    private static Context appContext;
    private static HandlerThread imageThread;
    private static Handler imageHandler;
    private static ImageReader imageReader;
    private static IScreenCaptureHost captureHost;
    private static ServiceConnection serviceConnection;
    private static boolean serviceBound;
    private static boolean surfaceAttached;
    private static long surfaceLeaseId;
    private static boolean hasFrame;
    private static int captureWidth;
    private static int captureHeight;
    private static int captureDensityDpi;

    private MediaProjectionScreenCaptureBridge() {
    }

    public static void initialize(Context context) {
        if (context == null) {
            return;
        }
        synchronized (STATE_LOCK) {
            if (appContext != null) {
                return;
            }
            Context application = context.getApplicationContext();
            appContext = application == null ? context : application;
            imageThread = new HandlerThread("MediaProjectionFrames");
            imageThread.start();
            imageHandler = new Handler(imageThread.getLooper());
            serviceConnection = new ServiceConnection() {
                @Override
                public void onServiceConnected(ComponentName name, IBinder service) {
                    synchronized (STATE_LOCK) {
                        captureHost = IScreenCaptureHost.Stub.asInterface(service);
                        surfaceAttached = false;
                        surfaceLeaseId = 0L;
                        STATE_LOCK.notifyAll();
                    }
                }

                @Override
                public void onServiceDisconnected(ComponentName name) {
                    clearHost();
                }

                @Override
                public void onBindingDied(ComponentName name) {
                    clearHost();
                }

                private void clearHost() {
                    synchronized (STATE_LOCK) {
                        captureHost = null;
                        surfaceAttached = false;
                        surfaceLeaseId = 0L;
                        STATE_LOCK.notifyAll();
                    }
                }
            };
            serviceBound = appContext.bindService(
                    new Intent(appContext, MediaProjectionCaptureService.class),
                    serviceConnection,
                    Context.BIND_AUTO_CREATE | Context.BIND_IMPORTANT
            );
        }
    }

    public static ScreenCaptureResult captureFrame(
            ByteBuffer targetBuffer,
            int targetCapacity,
            boolean allowCachedNativeFrame
    ) {
        long startTime = System.nanoTime();
        synchronized (CAPTURE_LOCK) {
            String prepareError = prepareCaptureLocked();
            if (prepareError != null) {
                return ScreenCaptureResult.failure(prepareError);
            }

            long cachedPixelBytes = (long) captureWidth * (long) captureHeight * 4L;
            boolean canReuseNativeFrame = hasFrame
                    && allowCachedNativeFrame
                    && cachedPixelBytes > 0L
                    && cachedPixelBytes <= Integer.MAX_VALUE
                    && targetBuffer != null
                    && targetBuffer.isDirect()
                    && targetCapacity >= (int) cachedPixelBytes
                    && targetBuffer.capacity() >= (int) cachedPixelBytes;
            Image image = acquireLatestImage(canReuseNativeFrame ? 0L : FIRST_FRAME_TIMEOUT_MS);
            if (image == null) {
                if (canReuseNativeFrame) {
                    return ScreenCaptureResult.successFromNativeBuffer(
                            targetBuffer,
                            (int) cachedPixelBytes,
                            captureWidth,
                            captureHeight,
                            "media-projection-cache",
                            elapsedMillis(startTime)
                    );
                }
                return ScreenCaptureResult.failure(
                        hasFrame
                                ? "等待非 Root 屏幕更新帧超时"
                                : "等待非 Root 屏幕首帧超时"
                );
            }

            try {
                return copyImageToResult(
                        image,
                        targetBuffer,
                        targetCapacity,
                        elapsedMillis(startTime)
                );
            } finally {
                image.close();
            }
        }
    }

    public static void shutdown() {
        synchronized (CAPTURE_LOCK) {
            IScreenCaptureHost host;
            synchronized (STATE_LOCK) {
                host = captureHost;
                surfaceAttached = false;
                long leaseId = surfaceLeaseId;
                surfaceLeaseId = 0L;
                if (host != null && leaseId != 0L) {
                    try {
                        host.detachSurface(leaseId);
                    } catch (RemoteException ignored) {
                        // 主进程或投屏会话可能已经结束。
                    }
                }
            }
            closeReaderLocked();
            synchronized (STATE_LOCK) {
                if (appContext != null && serviceBound && serviceConnection != null) {
                    try {
                        appContext.unbindService(serviceConnection);
                    } catch (IllegalArgumentException ignored) {
                        // Binder 已经断开。
                    }
                }
                serviceBound = false;
                captureHost = null;
                serviceConnection = null;
                appContext = null;
                if (imageThread != null) {
                    imageThread.quitSafely();
                }
                imageThread = null;
                imageHandler = null;
            }
        }
    }

    private static String prepareCaptureLocked() {
        Context context;
        Handler handler;
        synchronized (STATE_LOCK) {
            context = appContext;
            handler = imageHandler;
        }
        if (context == null || handler == null) {
            return "非 Root 截图 Worker 尚未初始化";
        }

        DisplayMetrics metrics = readDisplayMetrics(context);
        if (metrics.widthPixels <= 0 || metrics.heightPixels <= 0) {
            return "读取非 Root 屏幕尺寸失败";
        }
        if (imageReader == null
                || captureWidth != metrics.widthPixels
                || captureHeight != metrics.heightPixels
                || captureDensityDpi != metrics.densityDpi) {
            IScreenCaptureHost oldHost;
            synchronized (STATE_LOCK) {
                oldHost = captureHost;
                surfaceAttached = false;
                long oldLeaseId = surfaceLeaseId;
                surfaceLeaseId = 0L;
                if (oldHost != null && oldLeaseId != 0L) {
                    try {
                        oldHost.detachSurface(oldLeaseId);
                    } catch (RemoteException ignored) {
                        // 下一次连接会重新附着新 Surface。
                    }
                }
            }
            closeReaderLocked();
            captureWidth = metrics.widthPixels;
            captureHeight = metrics.heightPixels;
            captureDensityDpi = Math.max(1, metrics.densityDpi);
            imageReader = ImageReader.newInstance(
                    captureWidth,
                    captureHeight,
                    PixelFormat.RGBA_8888,
                    3
            );
            imageReader.setOnImageAvailableListener(reader -> {
                synchronized (FRAME_LOCK) {
                    FRAME_LOCK.notifyAll();
                }
            }, handler);
        }

        IScreenCaptureHost host = waitForHost();
        if (host == null) {
            return serviceBound
                    ? "非 Root 屏幕读取服务未连接"
                    : "非 Root 屏幕读取服务启动失败";
        }
        try {
            if (!host.isReady()) {
                return "非 Root 截图尚未授权，请在 App 中允许屏幕录制";
            }
            synchronized (STATE_LOCK) {
                if (!surfaceAttached) {
                    Surface surface = imageReader.getSurface();
                    surfaceLeaseId = host.attachSurface(
                            surface,
                            captureWidth,
                            captureHeight,
                            captureDensityDpi
                    );
                    surfaceAttached = surfaceLeaseId != 0L;
                }
                if (!surfaceAttached) {
                    return "非 Root 屏幕 Surface 附着失败";
                }
            }
            return null;
        } catch (RemoteException exception) {
            synchronized (STATE_LOCK) {
                captureHost = null;
                surfaceAttached = false;
                surfaceLeaseId = 0L;
            }
            return "非 Root 屏幕读取服务已断开";
        }
    }

    private static IScreenCaptureHost waitForHost() {
        long deadline = System.nanoTime() + HOST_CONNECT_TIMEOUT_MS * 1_000_000L;
        synchronized (STATE_LOCK) {
            while (captureHost == null && serviceBound) {
                long remainingMs = (deadline - System.nanoTime()) / 1_000_000L;
                if (remainingMs <= 0L) {
                    break;
                }
                try {
                    STATE_LOCK.wait(Math.max(1L, remainingMs));
                } catch (InterruptedException exception) {
                    Thread.currentThread().interrupt();
                    break;
                }
            }
            return captureHost;
        }
    }

    private static Image acquireLatestImage(long timeoutMs) {
        Image image = imageReader == null ? null : imageReader.acquireLatestImage();
        if (image != null || timeoutMs <= 0L) {
            return image;
        }
        long deadline = System.nanoTime() + timeoutMs * 1_000_000L;
        synchronized (FRAME_LOCK) {
            while (image == null) {
                image = imageReader == null ? null : imageReader.acquireLatestImage();
                if (image != null) {
                    break;
                }
                long remainingMs = (deadline - System.nanoTime()) / 1_000_000L;
                if (remainingMs <= 0L) {
                    break;
                }
                try {
                    FRAME_LOCK.wait(Math.max(1L, remainingMs));
                } catch (InterruptedException exception) {
                    Thread.currentThread().interrupt();
                    break;
                }
            }
        }
        return image;
    }

    private static ScreenCaptureResult copyImageToResult(
            Image image,
            ByteBuffer targetBuffer,
            int targetCapacity,
            long durationMs
    ) {
        Image.Plane[] planes = image.getPlanes();
        if (planes == null || planes.length == 0) {
            return ScreenCaptureResult.failure("非 Root 截图点阵平面为空");
        }
        Rect crop = image.getCropRect();
        int width = crop == null ? image.getWidth() : crop.width();
        int height = crop == null ? image.getHeight() : crop.height();
        if (width <= 0 || height <= 0) {
            return ScreenCaptureResult.failure("非 Root 截图尺寸无效");
        }
        if (crop != null
                && (crop.left < 0
                || crop.top < 0
                || crop.right > image.getWidth()
                || crop.bottom > image.getHeight())) {
            return ScreenCaptureResult.failure("非 Root 截图裁剪区域无效");
        }
        long pixelBytesLong = (long) width * (long) height * 4L;
        if (pixelBytesLong > Integer.MAX_VALUE) {
            return ScreenCaptureResult.failure("非 Root 截图尺寸过大");
        }
        int pixelBytes = (int) pixelBytesLong;
        boolean writeNative = targetBuffer != null
                && targetBuffer.isDirect()
                && targetCapacity >= pixelBytes
                && targetBuffer.capacity() >= pixelBytes;
        byte[] pixels = writeNative ? null : new byte[pixelBytes];
        ByteBuffer output = writeNative ? targetBuffer : ByteBuffer.wrap(pixels);
        output.clear();
        output.limit(pixelBytes);

        Image.Plane plane = planes[0];
        ByteBuffer source = plane.getBuffer().duplicate();
        int rowStride = plane.getRowStride();
        int pixelStride = plane.getPixelStride();
        int cropLeft = crop == null ? 0 : crop.left;
        int cropTop = crop == null ? 0 : crop.top;
        int sourceBase = source.position();
        if (pixelStride < 4 || rowStride <= 0 || sourceBase < 0) {
            return ScreenCaptureResult.failure("非 Root 截图点阵步长不受支持");
        }

        long minimumRowBytes = (long) cropLeft * pixelStride
                + (long) (width - 1) * pixelStride
                + 4L;
        if (minimumRowBytes > rowStride) {
            return ScreenCaptureResult.failure("非 Root 截图行步长不足");
        }

        long lastSourceByte = (long) sourceBase
                + (long) (cropTop + height - 1) * rowStride
                + (long) cropLeft * pixelStride
                + (long) (width - 1) * pixelStride
                + 4L;
        if (lastSourceByte > source.limit()) {
            return ScreenCaptureResult.failure("非 Root 截图点阵缓冲不完整");
        }

        try {
            for (int y = 0; y < height; ++y) {
                int sourceRow = sourceBase + (cropTop + y) * rowStride + cropLeft * pixelStride;
                if (pixelStride == 4) {
                    ByteBuffer row = source.duplicate();
                    row.position(sourceRow);
                    row.limit(sourceRow + width * 4);
                    output.put(row);
                } else {
                    for (int x = 0; x < width; ++x) {
                        int sourcePixel = sourceRow + x * pixelStride;
                        output.put(source.get(sourcePixel));
                        output.put(source.get(sourcePixel + 1));
                        output.put(source.get(sourcePixel + 2));
                        output.put(source.get(sourcePixel + 3));
                    }
                }
            }
        } catch (RuntimeException exception) {
            return ScreenCaptureResult.failure("复制非 Root 截图点阵失败：" + exception.getMessage());
        }

        output.position(0);
        hasFrame = true;
        if (writeNative) {
            targetBuffer.limit(targetBuffer.capacity());
            return ScreenCaptureResult.successFromNativeBuffer(
                    targetBuffer,
                    pixelBytes,
                    width,
                    height,
                    "media-projection",
                    durationMs
            );
        }
        return ScreenCaptureResult.successFromRgbaBytes(
                pixels,
                width,
                height,
                "media-projection",
                durationMs
        );
    }

    private static DisplayMetrics readDisplayMetrics(Context context) {
        DisplayMetrics metrics = new DisplayMetrics();
        WindowManager windowManager = (WindowManager) context.getSystemService(
                Context.WINDOW_SERVICE
        );
        if (windowManager != null) {
            windowManager.getDefaultDisplay().getRealMetrics(metrics);
        } else {
            metrics.setTo(context.getResources().getDisplayMetrics());
        }
        return metrics;
    }

    private static void closeReaderLocked() {
        if (imageReader != null) {
            imageReader.setOnImageAvailableListener(null, null);
            imageReader.close();
            imageReader = null;
        }
        hasFrame = false;
        captureWidth = 0;
        captureHeight = 0;
        captureDensityDpi = 0;
    }

    private static long elapsedMillis(long startTime) {
        return Math.max(0L, (System.nanoTime() - startTime) / 1_000_000L);
    }
}
