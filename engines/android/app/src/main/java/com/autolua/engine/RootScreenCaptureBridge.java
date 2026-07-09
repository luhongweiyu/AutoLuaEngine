/**
 * 文件用途：封装 Root 模式截图，优先通过 Root helper 获取原始像素。
 */
package com.autolua.engine;

import java.nio.ByteBuffer;

/**
 * Root 截图桥。
 *
 * 正式 root 截图先走 Surface/Window 隐藏 API 高速路线，不再把每帧 `su screencap`
 * 当作高频方案。旧的原始 screencap 解码只保留为显式调试入口，方便对比设备表现。
 */
public final class RootScreenCaptureBridge {
    private static final long CAPTURE_TIMEOUT_MS = 3000;
    private static final int LEGACY_HEADER_BYTES = 12;
    private static final int HEADER_BYTES = 16;
    private static final int FORMAT_RGBA_8888 = 1;
    private static final int FORMAT_RGBX_8888 = 2;
    private static final int FORMAT_BGRA_8888 = 5;

    private RootScreenCaptureBridge() {
    }

    public static ScreenCaptureResult captureFrame() {
        if (!RootShellBridge.isRootRuntimeReady()) {
            return ScreenCaptureResult.failure("root screen capture is not available");
        }

        return RootHelperBridge.captureFrame(0, 0);
    }

    public static ScreenCaptureResult captureRawScreencapForDebug() {
        if (!RootShellBridge.isRootRuntimeReady()) {
            return ScreenCaptureResult.failure("root screen capture is not available");
        }

        long startTime = System.nanoTime();
        RootShellBridge.CommandResult result =
                RootShellBridge.runRootBinaryCommand("screencap", CAPTURE_TIMEOUT_MS);
        long captureDurationMs = elapsedMillis(startTime);
        if (result.exitCode != 0) {
            String error = result.stderrText().trim();
            if (error.isEmpty()) {
                error = result.stdoutText().trim();
            }
            return ScreenCaptureResult.failure(
                    error.isEmpty() ? "root screencap failed" : "root screencap failed: " + error
            );
        }

        return decodeRawScreencap(result.stdout, captureDurationMs);
    }

    private static ScreenCaptureResult decodeRawScreencap(byte[] rawBytes, long captureDurationMs) {
        if (rawBytes == null || rawBytes.length < LEGACY_HEADER_BYTES) {
            return ScreenCaptureResult.failure("root screencap output is empty");
        }

        int width = readIntLittleEndian(rawBytes, 0);
        int height = readIntLittleEndian(rawBytes, 4);
        int format = readIntLittleEndian(rawBytes, 8);
        if (width <= 0 || height <= 0) {
            return ScreenCaptureResult.failure("root screencap size is invalid");
        }

        long pixelBytesLong = (long) width * (long) height * 4L;
        if (pixelBytesLong > Integer.MAX_VALUE) {
            return ScreenCaptureResult.failure("root screencap image is too large");
        }

        int pixelBytes = (int) pixelBytesLong;
        int pixelOffset = resolvePixelOffset(rawBytes.length, pixelBytes);
        if (pixelOffset <= 0) {
            return ScreenCaptureResult.failure("root screencap output size is invalid");
        }

        ByteBuffer rgbaBuffer = ByteBuffer.allocateDirect(pixelBytes);
        if (format == FORMAT_RGBA_8888) {
            rgbaBuffer.put(rawBytes, pixelOffset, pixelBytes);
        } else if (format == FORMAT_RGBX_8888) {
            copyRgbxToRgba(rawBytes, pixelOffset, rgbaBuffer, pixelBytes);
        } else if (format == FORMAT_BGRA_8888) {
            copyBgraToRgba(rawBytes, pixelOffset, rgbaBuffer, pixelBytes);
        } else {
            return ScreenCaptureResult.failure("root screencap format is unsupported: " + format);
        }
        rgbaBuffer.position(0);

        return ScreenCaptureResult.successFromRgbaBuffer(
                rgbaBuffer,
                width,
                height,
                "root-screencap",
                captureDurationMs
        );
    }

    private static long elapsedMillis(long startTime) {
        long elapsedNanos = System.nanoTime() - startTime;
        return Math.max(0L, elapsedNanos / 1_000_000L);
    }

    private static int resolvePixelOffset(int rawLength, int pixelBytes) {
        if (rawLength >= HEADER_BYTES + pixelBytes) {
            return HEADER_BYTES;
        }
        if (rawLength >= LEGACY_HEADER_BYTES + pixelBytes) {
            return LEGACY_HEADER_BYTES;
        }
        return -1;
    }

    private static int readIntLittleEndian(byte[] bytes, int offset) {
        return (bytes[offset] & 0xFF)
                | ((bytes[offset + 1] & 0xFF) << 8)
                | ((bytes[offset + 2] & 0xFF) << 16)
                | ((bytes[offset + 3] & 0xFF) << 24);
    }

    private static void copyRgbxToRgba(byte[] source, int sourceOffset, ByteBuffer target, int byteLength) {
        for (int offset = 0; offset < byteLength; offset += 4) {
            target.put(source[sourceOffset + offset]);
            target.put(source[sourceOffset + offset + 1]);
            target.put(source[sourceOffset + offset + 2]);
            target.put((byte) 0xFF);
        }
    }

    private static void copyBgraToRgba(byte[] source, int sourceOffset, ByteBuffer target, int byteLength) {
        for (int offset = 0; offset < byteLength; offset += 4) {
            target.put(source[sourceOffset + offset + 2]);
            target.put(source[sourceOffset + offset + 1]);
            target.put(source[sourceOffset + offset]);
            target.put(source[sourceOffset + offset + 3]);
        }
    }
}
