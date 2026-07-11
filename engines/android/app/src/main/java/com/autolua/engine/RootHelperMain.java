/**
 * 文件用途：Root helper 进程入口，在 su 环境中执行高频 Root 能力。
 */
package com.autolua.engine;

import android.graphics.Bitmap;
import android.util.DisplayMetrics;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import android.util.Base64;
import java.nio.charset.StandardCharsets;

/**
 * root helper 常驻进程入口。
 *
 * 这个类由 `su -c app_process ... com.autolua.engine.RootHelperMain` 启动，
 * 进程 uid=0，不属于普通 App 沙箱。App 的 `:engine` 进程通过 stdin/stdout
 * 与它通讯。协议为“文本头 + 原始二进制帧”，避免 base64 造成大块额外拷贝。
 * 第一版只实现截图命令，保持“启动一次、后续复用”的模型。
 */
public final class RootHelperMain {
    private static byte[] captureBytes;
    private static ByteBuffer captureBuffer;
    private static final RootInputInjector INPUT_INJECTOR = new RootInputInjector();

    private RootHelperMain() {
    }

    public static void main(String[] args) {
        try {
            runLoop();
        } catch (Throwable throwable) {
            writeLine("ERR\troot helper crashed: " + throwable.getMessage());
        }
    }

    private static void runLoop() throws Exception {
        InputStream inputStream = System.in;
        OutputStream outputStream = System.out;

        String line;
        while ((line = readLine(inputStream)) != null) {
            String[] parts = line.split("\t");
            if (parts.length == 0) {
                continue;
            }

            if ("ping".equals(parts[0])) {
                writeLine(outputStream, "OK\tpong");
                continue;
            }

            if ("exit".equals(parts[0])) {
                writeLine(outputStream, "OK\tbye");
                return;
            }

            if ("capture".equals(parts[0])) {
                handleCapture(outputStream, parts);
                continue;
            }

            if ("touchDown".equals(parts[0])) {
                writeBoolean(outputStream, handleTouchDown(parts));
                continue;
            }

            if ("touchMove".equals(parts[0])) {
                writeBoolean(outputStream, handleTouchMove(parts));
                continue;
            }

            if ("touchUp".equals(parts[0])) {
                writeBoolean(outputStream, handleTouchUp(parts));
                continue;
            }

            if ("keyDown".equals(parts[0])) {
                writeBoolean(outputStream, handleKeyDown(parts));
                continue;
            }

            if ("keyUp".equals(parts[0])) {
                writeBoolean(outputStream, handleKeyUp(parts));
                continue;
            }

            if ("keyPress".equals(parts[0])) {
                writeBoolean(outputStream, handleKeyPress(parts));
                continue;
            }

            if ("inputText".equals(parts[0])) {
                writeBoolean(outputStream, handleInputText(parts));
                continue;
            }

            writeLine(outputStream, "ERR\tunknown command");
        }
    }

    private static void handleCapture(OutputStream outputStream, String[] parts) throws Exception {
        int width = parts.length >= 2 ? parseInt(parts[1], 0) : 0;
        int height = parts.length >= 3 ? parseInt(parts[2], 0) : 0;
        if (width <= 0 || height <= 0) {
            DisplayMetrics metrics = RootHelperDisplayMetrics.read();
            width = metrics.widthPixels;
            height = metrics.heightPixels;
        }

        Bitmap bitmap = SurfaceScreenCaptureBridge.captureBitmapForRootHelper(width, height);
        if (bitmap == null) {
            writeLine(outputStream, "ERR\troot helper capture returned empty bitmap");
            return;
        }
        if (!Bitmap.Config.ARGB_8888.equals(bitmap.getConfig())) {
            Bitmap copy = bitmap.copy(Bitmap.Config.ARGB_8888, false);
            bitmap.recycle();
            if (copy == null) {
                writeLine(outputStream, "ERR\troot helper failed to copy hardware bitmap");
                return;
            }
            bitmap = copy;
        }

        int bitmapWidth = bitmap.getWidth();
        int bitmapHeight = bitmap.getHeight();
        int pixelBytes = bitmapWidth * bitmapHeight * 4;
        ByteBuffer pixels = ensureCaptureBuffer(pixelBytes);
        bitmap.copyPixelsToBuffer(pixels);
        bitmap.recycle();

        writeLine(outputStream, "OK\t"
                + bitmapWidth
                + "\t"
                + bitmapHeight
                + "\t"
                + pixelBytes);
        outputStream.write(captureBytes, 0, pixelBytes);
        outputStream.flush();
    }

    private static boolean handleTouchDown(String[] parts) {
        if (parts.length < 4) {
            return false;
        }
        return INPUT_INJECTOR.touchDown(
                parseInt(parts[1], -1),
                parseInt(parts[2], -1),
                parseInt(parts[3], -1)
        );
    }

    private static boolean handleTouchMove(String[] parts) {
        if (parts.length < 4) {
            return false;
        }
        return INPUT_INJECTOR.touchMove(
                parseInt(parts[1], -1),
                parseInt(parts[2], -1),
                parseInt(parts[3], -1)
        );
    }

    private static boolean handleTouchUp(String[] parts) {
        if (parts.length < 2) {
            return false;
        }
        return INPUT_INJECTOR.touchUp(parseInt(parts[1], -1));
    }

    private static boolean handleKeyDown(String[] parts) {
        if (parts.length < 2) {
            return false;
        }
        return INPUT_INJECTOR.keyDown(parseInt(parts[1], 0));
    }

    private static boolean handleKeyUp(String[] parts) {
        if (parts.length < 2) {
            return false;
        }
        return INPUT_INJECTOR.keyUp(parseInt(parts[1], 0));
    }

    private static boolean handleKeyPress(String[] parts) {
        if (parts.length < 2) {
            return false;
        }
        return INPUT_INJECTOR.keyPress(parseInt(parts[1], 0));
    }

    private static boolean handleInputText(String[] parts) {
        if (parts.length < 2) {
            return false;
        }
        try {
            byte[] bytes = Base64.decode(parts[1], Base64.NO_WRAP);
            return INPUT_INJECTOR.inputText(new String(bytes, StandardCharsets.UTF_8));
        } catch (IllegalArgumentException exception) {
            return false;
        }
    }

    /**
     * 复用 root helper 进程内的整帧缓冲。
     *
     * Bitmap.copyPixelsToBuffer 每次都需要一个可写 Buffer。这里让 ByteBuffer 包在同一块
     * byte[] 上，屏幕尺寸不变时不再反复分配整帧数组。
     */
    private static ByteBuffer ensureCaptureBuffer(int pixelBytes) {
        if (captureBytes == null || captureBytes.length < pixelBytes) {
            captureBytes = new byte[pixelBytes];
            captureBuffer = ByteBuffer.wrap(captureBytes).order(ByteOrder.nativeOrder());
        }

        captureBuffer.clear();
        captureBuffer.limit(pixelBytes);
        return captureBuffer;
    }

    private static String readLine(InputStream inputStream) throws Exception {
        ByteArrayOutputStream outputStream = new ByteArrayOutputStream(128);
        int value;
        while ((value = inputStream.read()) != -1) {
            if (value == '\n') {
                return outputStream.toString(StandardCharsets.UTF_8.name());
            }
            if (value != '\r') {
                outputStream.write(value);
            }
        }
        return outputStream.size() == 0
                ? null
                : outputStream.toString(StandardCharsets.UTF_8.name());
    }

    private static void writeLine(OutputStream outputStream, String text) throws Exception {
        outputStream.write((text + "\n").getBytes(StandardCharsets.UTF_8));
        outputStream.flush();
    }

    private static void writeBoolean(OutputStream outputStream, boolean value) throws Exception {
        writeLine(outputStream, value ? "OK\ttrue" : "OK\tfalse");
    }

    private static int parseInt(String value, int defaultValue) {
        try {
            return Integer.parseInt(value);
        } catch (NumberFormatException exception) {
            return defaultValue;
        }
    }

    private static void writeLine(String text) {
        try {
            System.out.write((text + "\n").getBytes(StandardCharsets.UTF_8));
            System.out.flush();
        } catch (Exception ignored) {
            // helper 即将退出时不再递归处理输出错误。
        }
    }
}
