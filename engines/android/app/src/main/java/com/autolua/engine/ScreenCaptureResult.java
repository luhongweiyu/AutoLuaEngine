package com.autolua.engine;

import android.media.Image;

import java.nio.ByteBuffer;

/**
 * 截图结果对象。
 *
 * Java 层负责拿到 Android ImageReader 的帧，Native 层直接读取 ByteBuffer，
 * 复制到 native 图片句柄后关闭这一帧。这样避免 PNG 编码和磁盘 IO。
 */
public final class ScreenCaptureResult {
    public final boolean success;
    public final ByteBuffer pixelBuffer;
    public final int width;
    public final int height;
    public final int rowStride;
    public final int pixelStride;
    public final String format;
    public final String source;
    public final long captureDurationMs;
    public final String error;

    private final Image image;
    private boolean closed;

    private ScreenCaptureResult(
            boolean success,
            Image image,
            ByteBuffer pixelBuffer,
            int width,
            int height,
            int rowStride,
            int pixelStride,
            String format,
            String source,
            long captureDurationMs,
            String error
    ) {
        this.success = success;
        this.image = image;
        this.pixelBuffer = pixelBuffer;
        this.width = width;
        this.height = height;
        this.rowStride = rowStride;
        this.pixelStride = pixelStride;
        this.format = format;
        this.source = source;
        this.captureDurationMs = captureDurationMs;
        this.error = error;
    }

    public static ScreenCaptureResult successFromImage(
            Image image,
            int width,
            int height,
            String source,
            long captureDurationMs
    ) {
        Image.Plane[] planes = image.getPlanes();
        if (planes.length == 0) {
            return failure("screen capture image has no pixel plane");
        }

        Image.Plane plane = planes[0];
        ByteBuffer buffer = plane.getBuffer();
        if (buffer == null || !buffer.isDirect()) {
            return failure("screen capture pixel buffer is not direct");
        }

        int rowStride = plane.getRowStride();
        int pixelStride = plane.getPixelStride();
        if (rowStride <= 0 || pixelStride <= 0) {
            return failure("screen capture pixel stride is invalid");
        }

        buffer.position(0);
        return new ScreenCaptureResult(
                true,
                image,
                buffer,
                width,
                height,
                rowStride,
                pixelStride,
                "rgba8888",
                source,
                captureDurationMs,
                null
        );
    }

    public static ScreenCaptureResult successFromRgbaBytes(
            byte[] pixels,
            int width,
            int height,
            String source,
            long captureDurationMs
    ) {
        if (pixels == null || pixels.length == 0) {
            return failure("screen capture pixel buffer is empty");
        }

        if (width <= 0 || height <= 0) {
            return failure("screen capture size is invalid");
        }

        long expectedLengthLong = (long) width * (long) height * 4L;
        if (expectedLengthLong > Integer.MAX_VALUE) {
            return failure("screen capture pixel buffer is too large");
        }

        int rowStride = width * 4;
        int expectedLength = (int) expectedLengthLong;
        if (pixels.length < expectedLength) {
            return failure("screen capture pixel buffer is incomplete");
        }

        ByteBuffer buffer = ByteBuffer.allocateDirect(expectedLength);
        buffer.put(pixels, 0, expectedLength);
        buffer.position(0);

        return new ScreenCaptureResult(
                true,
                null,
                buffer,
                width,
                height,
                rowStride,
                4,
                "rgba8888",
                source,
                captureDurationMs,
                null
        );
    }

    public static ScreenCaptureResult successFromRgbaBuffer(
            ByteBuffer buffer,
            int width,
            int height,
            String source,
            long captureDurationMs
    ) {
        if (buffer == null || !buffer.isDirect()) {
            return failure("screen capture pixel buffer is not direct");
        }

        if (width <= 0 || height <= 0) {
            return failure("screen capture size is invalid");
        }

        long expectedLengthLong = (long) width * (long) height * 4L;
        if (expectedLengthLong > Integer.MAX_VALUE) {
            return failure("screen capture pixel buffer is too large");
        }

        int expectedLength = (int) expectedLengthLong;
        if (buffer.capacity() < expectedLength) {
            return failure("screen capture pixel buffer is incomplete");
        }

        buffer.position(0);
        return new ScreenCaptureResult(
                true,
                null,
                buffer,
                width,
                height,
                width * 4,
                4,
                "rgba8888",
                source,
                captureDurationMs,
                null
        );
    }

    public static ScreenCaptureResult failure(String error) {
        return new ScreenCaptureResult(false, null, null, 0, 0, 0, 0, null, "", 0, error);
    }

    public void close() {
        if (closed) {
            return;
        }

        closed = true;
        if (image != null) {
            image.close();
        }
    }
}
