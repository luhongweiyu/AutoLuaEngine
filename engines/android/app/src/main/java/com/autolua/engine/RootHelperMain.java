package com.autolua.engine;

import android.graphics.Bitmap;
import android.util.DisplayMetrics;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;

/**
 * root helper 常驻进程入口。
 *
 * 这个类由 `su -c app_process ... com.autolua.engine.RootHelperMain` 启动，
 * 进程 uid=0，不属于普通 App 沙箱。App 的 `:engine` 进程通过 stdin/stdout
 * 与它通讯。协议为“文本头 + 原始二进制帧”，避免 base64 造成大块额外拷贝。
 * 第一版只实现截图命令，后续点击、文件、设备控制等 root 能力会逐步下沉到这里，
 * 保持“启动一次、后续复用”的模型。
 */
public final class RootHelperMain {
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
        ByteBuffer pixels = ByteBuffer.allocate(pixelBytes).order(ByteOrder.nativeOrder());
        bitmap.copyPixelsToBuffer(pixels);
        bitmap.recycle();

        writeLine(outputStream, "OK\t"
                + bitmapWidth
                + "\t"
                + bitmapHeight
                + "\t"
                + pixelBytes);
        outputStream.write(pixels.array());
        outputStream.flush();
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
