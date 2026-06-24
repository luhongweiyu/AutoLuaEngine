package com.autolua.engine;

/**
 * Root 截图桥。
 *
 * 这里使用 `screencap` 的原始输出，不使用 `screencap -p`，避免 PNG 编码和磁盘 IO。
 * 这仍然是短命令方案，不适合最终高频找色；后续高频路线应升级为常驻 root 进程。
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
        if (!RootShellBridge.isRootAvailable()) {
            return ScreenCaptureResult.failure("root screen capture is not available");
        }

        long startTime = System.nanoTime();
        RootShellBridge.CommandResult result =
                RootShellBridge.runRootCommand("screencap", CAPTURE_TIMEOUT_MS);
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

        byte[] rgbaPixels = new byte[pixelBytes];
        if (format == FORMAT_RGBA_8888) {
            System.arraycopy(rawBytes, pixelOffset, rgbaPixels, 0, pixelBytes);
        } else if (format == FORMAT_RGBX_8888) {
            copyRgbxToRgba(rawBytes, pixelOffset, rgbaPixels, pixelBytes);
        } else if (format == FORMAT_BGRA_8888) {
            copyBgraToRgba(rawBytes, pixelOffset, rgbaPixels, pixelBytes);
        } else {
            return ScreenCaptureResult.failure("root screencap format is unsupported: " + format);
        }

        return ScreenCaptureResult.successFromRgbaBytes(
                rgbaPixels,
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

    private static void copyRgbxToRgba(byte[] source, int sourceOffset, byte[] target, int byteLength) {
        for (int offset = 0; offset < byteLength; offset += 4) {
            target[offset] = source[sourceOffset + offset];
            target[offset + 1] = source[sourceOffset + offset + 1];
            target[offset + 2] = source[sourceOffset + offset + 2];
            target[offset + 3] = (byte) 0xFF;
        }
    }

    private static void copyBgraToRgba(byte[] source, int sourceOffset, byte[] target, int byteLength) {
        for (int offset = 0; offset < byteLength; offset += 4) {
            target[offset] = source[sourceOffset + offset + 2];
            target[offset + 1] = source[sourceOffset + offset + 1];
            target[offset + 2] = source[sourceOffset + offset];
            target[offset + 3] = source[sourceOffset + offset + 3];
        }
    }
}
