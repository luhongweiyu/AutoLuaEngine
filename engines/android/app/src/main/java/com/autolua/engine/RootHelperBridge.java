package com.autolua.engine;

import java.io.BufferedWriter;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStreamWriter;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;

/**
 * App 引擎进程访问 root helper 的桥。
 *
 * helper 由 `su -c app_process` 启动一次，后续命令通过 stdin/stdout 传输。
 * 第一版只承接高频截图验证；等截图路线稳定后，再把点击、文件、设备控制等
 * root API 逐步搬进 helper，减少 Java 侧 shell 命令依赖。
 */
public final class RootHelperBridge {
    private static final Object LOCK = new Object();
    private static RootHelperSession session;

    private RootHelperBridge() {
    }

    public static ScreenCaptureResult captureFrame(int width, int height) {
        synchronized (LOCK) {
            try {
                RootHelperSession helper = ensureSessionLocked();
                long startTime = System.nanoTime();
                RootHelperResponse response = helper.request(
                        "capture\t" + width + "\t" + height,
                        5000
                );
                long durationMs = elapsedMillis(startTime);
                if (!response.ok) {
                    return ScreenCaptureResult.failure(response.message);
                }

                String[] fields = response.message.split("\t", 3);
                if (fields.length != 3) {
                    return ScreenCaptureResult.failure("root helper capture response is invalid");
                }

                int frameWidth = parseInt(fields[0], 0);
                int frameHeight = parseInt(fields[1], 0);
                byte[] pixels = response.pixelBytes;
                ByteBuffer buffer = ByteBuffer.allocateDirect(pixels.length);
                buffer.put(pixels);
                buffer.position(0);
                return ScreenCaptureResult.successFromRgbaBuffer(
                        buffer,
                        frameWidth,
                        frameHeight,
                        "root-helper",
                        durationMs
                );
            } catch (IOException | RuntimeException exception) {
                closeSessionLocked();
                return ScreenCaptureResult.failure("root helper 截图失败：" + exception.getMessage());
            }
        }
    }

    private static RootHelperSession ensureSessionLocked() throws IOException {
        if (session != null && session.isAlive()) {
            return session;
        }

        closeSessionLocked();
        session = RootHelperSession.start();
        RootHelperResponse response = session.request("ping", 2500);
        if (!response.ok) {
            closeSessionLocked();
            throw new IOException(response.message);
        }
        return session;
    }

    private static void closeSessionLocked() {
        if (session != null) {
            session.close();
            session = null;
        }
    }

    private static int parseInt(String value, int defaultValue) {
        try {
            return Integer.parseInt(value);
        } catch (NumberFormatException exception) {
            return defaultValue;
        }
    }

    private static long elapsedMillis(long startTime) {
        long elapsedNanos = System.nanoTime() - startTime;
        return Math.max(0L, elapsedNanos / 1_000_000L);
    }

    private static final class RootHelperSession {
        private final Process process;
        private final BufferedWriter writer;
        private final InputStream rawReader;

        private RootHelperSession(Process process) {
            this.process = process;
            this.rawReader = process.getInputStream();
            this.writer = new BufferedWriter(
                    new OutputStreamWriter(process.getOutputStream(), StandardCharsets.UTF_8)
            );
            startStderrDrainer(process.getErrorStream());
        }

        private static RootHelperSession start() throws IOException {
            String classPath = AndroidHostBridge.appContext().getPackageCodePath();
            String command = "CLASSPATH="
                    + RootShellBridge.shellQuote(classPath)
                    + " app_process /system/bin com.autolua.engine.RootHelperMain";
            Process process = new ProcessBuilder("su", "-c", command).start();
            return new RootHelperSession(process);
        }

        private boolean isAlive() {
            try {
                process.exitValue();
                return false;
            } catch (IllegalThreadStateException ignored) {
                return true;
            }
        }

        private RootHelperResponse request(String command, long timeoutMs) throws IOException {
            writer.write(command);
            writer.write('\n');
            writer.flush();

            long deadline = System.currentTimeMillis() + timeoutMs;
            while (System.currentTimeMillis() < deadline) {
                if (rawReader.available() > 0) {
                    String line = readLine(rawReader);
                    if (line == null) {
                        return RootHelperResponse.error("root helper 已退出");
                    }
                    if (line.startsWith("OK\t")) {
                        String message = line.substring(3);
                        byte[] pixelBytes = readPixelBytesIfNeeded(message);
                        return RootHelperResponse.ok(message, pixelBytes);
                    }
                    if (line.startsWith("ERR\t")) {
                        return RootHelperResponse.error(line.substring(4));
                    }
                    continue;
                }

                try {
                    Thread.sleep(10);
                } catch (InterruptedException exception) {
                    Thread.currentThread().interrupt();
                    return RootHelperResponse.error("root helper 请求被中断");
                }
            }

            return RootHelperResponse.error("root helper 请求超时");
        }

        private String readLine(InputStream inputStream) throws IOException {
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

        private byte[] readPixelBytesIfNeeded(String message) throws IOException {
            String[] fields = message.split("\t");
            if (fields.length != 3) {
                return new byte[0];
            }

            int byteLength = parseInt(fields[2], 0);
            if (byteLength <= 0) {
                return new byte[0];
            }

            byte[] bytes = new byte[byteLength];
            int offset = 0;
            while (offset < byteLength) {
                int readCount = rawReader.read(bytes, offset, byteLength - offset);
                if (readCount < 0) {
                    throw new IOException("root helper pixel stream ended");
                }
                offset += readCount;
            }
            return bytes;
        }

        private void startStderrDrainer(InputStream inputStream) {
            Thread thread = new Thread(() -> {
                try (InputStream source = inputStream) {
                    byte[] buffer = new byte[1024];
                    while (source.read(buffer) != -1) {
                        // app_process/su 的 stderr 只需要排空，错误会通过协议返回。
                    }
                } catch (IOException ignored) {
                    // helper 退出时自然结束。
                }
            }, "RootHelperStderr");
            thread.setDaemon(true);
            thread.start();
        }


        private void close() {
            try {
                writer.write("exit\n");
                writer.flush();
            } catch (IOException ignored) {
                // helper 可能已经退出。
            }
            process.destroy();
        }
    }

    private static final class RootHelperResponse {
        private final boolean ok;
        private final String message;
        private final byte[] pixelBytes;

        private RootHelperResponse(boolean ok, String message, byte[] pixelBytes) {
            this.ok = ok;
            this.message = message == null ? "" : message;
            this.pixelBytes = pixelBytes == null ? new byte[0] : pixelBytes;
        }

        private static RootHelperResponse ok(String message, byte[] pixelBytes) {
            return new RootHelperResponse(true, message, pixelBytes);
        }

        private static RootHelperResponse error(String message) {
            return new RootHelperResponse(false, message, new byte[0]);
        }
    }
}
