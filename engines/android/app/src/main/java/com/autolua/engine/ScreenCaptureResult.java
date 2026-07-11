/**
 * 文件用途：Root helper 截图结果数据结构，承载 RGBA 像素、尺寸、耗时和错误信息。
 */
package com.autolua.engine;

import java.nio.ByteBuffer;

/**
 * 截图结果对象。
 *
 * Java 层把 Root helper 得到的原始 RGBA 像素直接交给 Native 层。
 */
public final class ScreenCaptureResult {
    public final boolean success;
    public final byte[] pixels;
    public final int pixelBytes;
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
            byte[] pixels,
            int pixelBytes,
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
        this.pixels = pixels;
        this.pixelBytes = pixelBytes;
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

        return new ScreenCaptureResult(
                true,
                pixels,
                expectedLength,
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

    public static ScreenCaptureResult successFromNativeBuffer(
            ByteBuffer targetBuffer,
            int pixelBytes,
            int width,
            int height,
            String source,
            long captureDurationMs
    ) {
        if (targetBuffer == null || !targetBuffer.isDirect()) {
            return failure("screen capture native buffer is not direct");
        }

        if (width <= 0 || height <= 0) {
            return failure("screen capture size is invalid");
        }

        long expectedLengthLong = (long) width * (long) height * 4L;
        if (expectedLengthLong > Integer.MAX_VALUE) {
            return failure("screen capture pixel buffer is too large");
        }

        int expectedLength = (int) expectedLengthLong;
        if (pixelBytes < expectedLength || targetBuffer.capacity() < expectedLength) {
            return failure("screen capture native buffer is incomplete");
        }

        targetBuffer.position(0);
        return new ScreenCaptureResult(
                true,
                null,
                expectedLength,
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
        byte[] pixels = new byte[expectedLength];
        buffer.get(pixels, 0, expectedLength);
        return new ScreenCaptureResult(
                true,
                pixels,
                expectedLength,
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
        return new ScreenCaptureResult(false, null, 0, 0, 0, 0, 0, null, "", 0, error);
    }

    public void close() {
        if (closed) {
            return;
        }

        closed = true;
    }
}
