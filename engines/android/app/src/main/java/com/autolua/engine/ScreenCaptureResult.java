/**
 * 文件用途：Root helper 截图结果数据结构，承载 RGBA 像素、尺寸、耗时和错误信息。
 */
package com.autolua.engine;

import java.nio.ByteBuffer;

/**
 * 截图结果对象。
 *
 * Java 层把 Root helper 得到的 Bitmap 像素整理成 direct ByteBuffer，Native 层
 * 直接复制该缓冲到 libengine.so 内部截图缓存，避免 PNG 编码和磁盘 IO。
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

    private boolean closed;

    private ScreenCaptureResult(
            boolean success,
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
        return new ScreenCaptureResult(false, null, 0, 0, 0, 0, null, "", 0, error);
    }

    public void close() {
        if (closed) {
            return;
        }

        closed = true;
    }
}
