/**
 * 文件用途：管理 :engine 到常驻 RootDaemon 的本地会话，用于高效执行 Root 能力。
 */
package com.xiaoyv.engine;

import java.io.BufferedWriter;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStreamWriter;
import java.nio.ByteBuffer;
import java.nio.channels.Channels;
import java.nio.channels.ReadableByteChannel;
import java.nio.charset.StandardCharsets;
import java.net.Socket;
import android.util.Base64;

/**
 * App 引擎进程访问 root helper 的桥。
 *
 * RootDaemon 由 App 主进程提前通过 `su -c app_process` 启动。:engine 这里只保持一个
 * 已认证 socket 会话；强停 :engine 只会断开本客户端，不会结束 RootDaemon 或重新执行 su。
 * 截图和输入注入都走该二进制安全通道，不为每个脚本命令拉起外部进程。
 */
public final class RootHelperBridge {
    private static final Object LOCK = new Object();
    private static RootHelperSession session;

    private RootHelperBridge() {
    }

    public static void shutdown() {
        synchronized (LOCK) {
            closeSessionLocked();
        }
    }

    public static boolean prepare() {
        synchronized (LOCK) {
            try {
                ensureSessionLocked();
                return true;
            } catch (IOException | RuntimeException exception) {
                closeSessionLocked();
                return false;
            }
        }
    }

    public static ScreenCaptureResult captureFrame(
            int width,
            int height,
            ByteBuffer targetBuffer,
            int targetCapacity
    ) {
        synchronized (LOCK) {
            try {
                RootHelperSession helper = ensureSessionLocked();
                long startTime = System.nanoTime();
                RootHelperResponse response = helper.request(
                        "capture\t" + width + "\t" + height,
                        5000,
                        targetBuffer,
                        targetCapacity
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
                if (response.wroteToTargetBuffer) {
                    return ScreenCaptureResult.successFromNativeBuffer(
                            targetBuffer,
                            response.pixelByteLength,
                            frameWidth,
                            frameHeight,
                            "root-helper",
                            durationMs
                    );
                }

                return ScreenCaptureResult.successFromRgbaBytes(
                        response.pixelBytes,
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

    public static boolean touchDown(int id, int x, int y) {
        return requestBooleanCommand("touchDown\t" + id + "\t" + x + "\t" + y, 1000);
    }

    public static boolean touchMove(int id, int x, int y) {
        return requestBooleanCommand("touchMove\t" + id + "\t" + x + "\t" + y, 1000);
    }

    public static boolean touchUp(int id) {
        return requestBooleanCommand("touchUp\t" + id, 1000);
    }

    public static boolean keyDown(int keyCode) {
        return requestBooleanCommand("keyDown\t" + keyCode, 1000);
    }

    public static boolean keyUp(int keyCode) {
        return requestBooleanCommand("keyUp\t" + keyCode, 1000);
    }

    public static boolean keyPress(int keyCode) {
        return requestBooleanCommand("keyPress\t" + keyCode, 1000);
    }

    public static boolean inputText(String text) {
        String safeText = text == null ? "" : text;
        String encoded = Base64.encodeToString(
                safeText.getBytes(StandardCharsets.UTF_8),
                Base64.NO_WRAP
        );
        return requestBooleanCommand("inputText\t" + encoded, 5000);
    }

    /**
     * 保存当前默认输入法并切换到 小鱼精灵 输入法。
     *
     * 返回值是 Base64 协议解码后的原输入法组件名；失败时返回 null。该操作只在
     * imeLib.lock 调用时执行一次系统切换，不参与高频文本提交。
     */
    public static String lockInputMethod(String engineInputMethod) {
        if (!EngineImeBridge.inputMethodComponent().equals(engineInputMethod)) {
            return null;
        }

        String encodedPrevious = requestStringCommand("imeLock", 5000);
        if (encodedPrevious == null || encodedPrevious.isEmpty()) {
            return null;
        }

        try {
            return new String(
                    Base64.decode(encodedPrevious, Base64.NO_WRAP),
                    StandardCharsets.UTF_8
            );
        } catch (IllegalArgumentException exception) {
            return null;
        }
    }

    /**
     * 恢复 lock 前默认输入法并禁用 小鱼精灵 输入法。
     */
    public static boolean unlockInputMethod(
            String previousInputMethod,
            String engineInputMethod
    ) {
        if (previousInputMethod == null
                || previousInputMethod.isEmpty()
                || !EngineImeBridge.inputMethodComponent().equals(engineInputMethod)) {
            return false;
        }

        String encodedPrevious = Base64.encodeToString(
                previousInputMethod.getBytes(StandardCharsets.UTF_8),
                Base64.NO_WRAP
        );
        String encodedEngine = Base64.encodeToString(
                engineInputMethod.getBytes(StandardCharsets.UTF_8),
                Base64.NO_WRAP
        );
        return requestBooleanCommand(
                "imeUnlock\t" + encodedPrevious + "\t" + encodedEngine,
                5000
        );
    }

    private static boolean requestBooleanCommand(String command, long timeoutMs) {
        synchronized (LOCK) {
            try {
                RootHelperSession helper = ensureSessionLocked();
                RootHelperResponse response = helper.request(command, timeoutMs, null, 0);
                return response.ok && "true".equals(response.message);
            } catch (IOException | RuntimeException exception) {
                closeSessionLocked();
                return false;
            }
        }
    }

    /**
     * 发送返回文本的 Root helper 命令。
     */
    private static String requestStringCommand(String command, long timeoutMs) {
        synchronized (LOCK) {
            try {
                RootHelperSession helper = ensureSessionLocked();
                RootHelperResponse response = helper.request(command, timeoutMs, null, 0);
                return response.ok ? response.message : null;
            } catch (IOException | RuntimeException exception) {
                closeSessionLocked();
                return null;
            }
        }
    }

    private static RootHelperSession ensureSessionLocked() throws IOException {
        if (session != null && session.isAlive()) {
            return session;
        }

        closeSessionLocked();
        session = RootHelperSession.start();
        RootHelperResponse response = session.request("ping", 2500, null, 0);
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
        private final Socket socket;
        private final BufferedWriter writer;
        private final InputStream rawReader;
        private final ReadableByteChannel rawChannel;

        private RootHelperSession(Socket socket) throws IOException {
            this.socket = socket;
            this.rawReader = socket.getInputStream();
            this.rawChannel = Channels.newChannel(rawReader);
            this.writer = new BufferedWriter(
                    new OutputStreamWriter(socket.getOutputStream(), StandardCharsets.UTF_8)
            );
        }

        private static RootHelperSession start() throws IOException {
            return new RootHelperSession(RootDaemonClient.openAuthenticatedSocket(
                    AndroidHostBridge.appContext(),
                    RootDaemonProtocol.CONNECT_TIMEOUT_MS
            ));
        }

        private boolean isAlive() {
            return socket.isConnected() && !socket.isClosed();
        }

        private RootHelperResponse request(
                String command,
                long timeoutMs,
                ByteBuffer targetBuffer,
                int targetCapacity
        ) throws IOException {
            writer.write(command);
            writer.write('\n');
            writer.flush();

            // 直接阻塞等待 socket 响应，避免旧轮询方案每条命令额外等待 0 到 10ms。
            // 响应头及紧随其后的每次原始像素流读取都受 socket 超时保护。
            socket.setSoTimeout(toSocketTimeout(timeoutMs));
            try {
                String line = readLine(rawReader);
                if (line == null) {
                    return RootHelperResponse.error("RootDaemon 已关闭");
                }
                if (line.startsWith("OK\t")) {
                    String message = line.substring(3);
                    RootHelperPayload payload = readPixelBytesIfNeeded(
                            message,
                            targetBuffer,
                            targetCapacity
                    );
                    return RootHelperResponse.ok(message, payload);
                }
                if (line.startsWith("ERR\t")) {
                    return RootHelperResponse.error(line.substring(4));
                }
                return RootHelperResponse.error("RootDaemon 响应无效");
            } finally {
                // 空闲 socket 不保留读超时；下一条命令按自己的时限重新设置。
                socket.setSoTimeout(0);
            }
        }

        /**
         * Socket 超时以 int 表示。业务超时均很短，但此处仍做上限保护，避免 long 转换溢出。
         */
        private int toSocketTimeout(long timeoutMs) {
            if (timeoutMs <= 0L) {
                return 1;
            }
            return (int) Math.min(timeoutMs, Integer.MAX_VALUE);
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

        private RootHelperPayload readPixelBytesIfNeeded(
                String message,
                ByteBuffer targetBuffer,
                int targetCapacity
        ) throws IOException {
            String[] fields = message.split("\t");
            if (fields.length != 3) {
                return RootHelperPayload.empty();
            }

            int byteLength = parseInt(fields[2], 0);
            if (byteLength <= 0) {
                return RootHelperPayload.empty();
            }

            if (targetBuffer != null
                    && targetBuffer.isDirect()
                    && targetCapacity >= byteLength
                    && targetBuffer.capacity() >= byteLength) {
                targetBuffer.position(0);
                targetBuffer.limit(byteLength);
                while (targetBuffer.hasRemaining()) {
                    int readCount = rawChannel.read(targetBuffer);
                    if (readCount < 0) {
                        throw new IOException("root helper pixel stream ended");
                    }
                }
                targetBuffer.position(0);
                targetBuffer.limit(targetBuffer.capacity());
                return RootHelperPayload.nativeBuffer(byteLength);
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
            return RootHelperPayload.bytes(bytes);
        }

        private void close() {
            try {
                socket.close();
            } catch (IOException ignored) {
                // :engine 退出或 RootDaemon 已关闭时 socket 可能已经断开。
            }
        }
    }

    private static final class RootHelperResponse {
        private final boolean ok;
        private final String message;
        private final byte[] pixelBytes;
        private final int pixelByteLength;
        private final boolean wroteToTargetBuffer;

        private RootHelperResponse(boolean ok, String message, RootHelperPayload payload) {
            this.ok = ok;
            this.message = message == null ? "" : message;
            RootHelperPayload actualPayload = payload == null ? RootHelperPayload.empty() : payload;
            this.pixelBytes = actualPayload.pixelBytes;
            this.pixelByteLength = actualPayload.pixelByteLength;
            this.wroteToTargetBuffer = actualPayload.wroteToTargetBuffer;
        }

        private static RootHelperResponse ok(String message, RootHelperPayload payload) {
            return new RootHelperResponse(true, message, payload);
        }

        private static RootHelperResponse error(String message) {
            return new RootHelperResponse(false, message, RootHelperPayload.empty());
        }
    }

    private static final class RootHelperPayload {
        private final byte[] pixelBytes;
        private final int pixelByteLength;
        private final boolean wroteToTargetBuffer;

        private RootHelperPayload(byte[] pixelBytes, int pixelByteLength, boolean wroteToTargetBuffer) {
            this.pixelBytes = pixelBytes == null ? new byte[0] : pixelBytes;
            this.pixelByteLength = pixelByteLength;
            this.wroteToTargetBuffer = wroteToTargetBuffer;
        }

        private static RootHelperPayload empty() {
            return new RootHelperPayload(new byte[0], 0, false);
        }

        private static RootHelperPayload bytes(byte[] pixelBytes) {
            return new RootHelperPayload(pixelBytes, pixelBytes == null ? 0 : pixelBytes.length, false);
        }

        private static RootHelperPayload nativeBuffer(int pixelByteLength) {
            return new RootHelperPayload(new byte[0], pixelByteLength, true);
        }
    }
}
