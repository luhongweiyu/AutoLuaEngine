/**
 * 文件用途：管理当前进程到常驻 RootDaemon 的本地会话，用于高效执行 Root 能力。
 */
package com.xiaoyv.engine;

import java.io.BufferedWriter;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStreamWriter;
import java.nio.charset.StandardCharsets;
import java.net.Socket;
import android.util.Base64;

/**
 * 本次脚本 Worker 或 App UID 宿主访问 root helper 的桥。
 *
 * RootDaemon 由 App 主进程提前通过 `su -c app_process` 启动。每个调用进程各自保持一个
 * 已认证 socket 会话；强停 Worker 只会断开本客户端，不会结束 RootDaemon 或重新执行 su。
 * 输入注入和系统命令走该通道，不为每个脚本命令拉起外部进程。截图由 uid=0 Worker
 * 在本进程直接完成，不经过此 socket。
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
     * 执行一条脚本显式请求的 Root shell 命令。
     *
     * 该入口仅负责维持与常驻 RootDaemon 的认证会话和协议编码，不检查命令退出码、不尝试
     * 其他命令。Shell 的合并输出原样返回，脚本可按自己的业务规则判断结果。
     */
    public static ShellResult executeShell(String command) {
        if (command == null || command.isEmpty()) {
            return ShellResult.failure("命令不能为空");
        }

        String encoded = Base64.encodeToString(
                command.getBytes(StandardCharsets.UTF_8),
                Base64.NO_WRAP
        );
        synchronized (LOCK) {
            try {
                RootHelperSession helper = ensureSessionLocked();
                // exec 的阻塞时长由脚本命令本身决定；socket 使用无限等待，避免框架替用户
                // 截断一个合法的长时间命令。
                RootHelperResponse response = helper.request("exec\t" + encoded, 0);
                if (!response.ok) {
                    return ShellResult.failure(response.message);
                }
                try {
                    String output = new String(
                            Base64.decode(response.message, Base64.NO_WRAP),
                            StandardCharsets.UTF_8
                    );
                    return ShellResult.success(output);
                } catch (IllegalArgumentException exception) {
                    return ShellResult.failure("Root 命令输出编码无效");
                }
            } catch (IOException | RuntimeException exception) {
                closeSessionLocked();
                return ShellResult.failure("Root 命令执行失败：" + exception.getMessage());
            }
        }
    }

    /**
     * 保存当前默认输入法并切换到 小鱼精灵 输入法。
     *
     * 返回值是 Base64 协议解码后的原输入法组件名；失败时返回 null。该操作只在
     * m.ime.lock 调用时执行一次系统切换，不参与高频文本提交。
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
                RootHelperResponse response = helper.request(command, timeoutMs);
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
                RootHelperResponse response = helper.request(command, timeoutMs);
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

    private static final class RootHelperSession {
        private final Socket socket;
        private final BufferedWriter writer;
        private final InputStream rawReader;

        private RootHelperSession(Socket socket) throws IOException {
            this.socket = socket;
            this.rawReader = socket.getInputStream();
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

        private RootHelperResponse request(String command, long timeoutMs) throws IOException {
            writer.write(command);
            writer.write('\n');
            writer.flush();

            // 直接阻塞等待 socket 响应，避免旧轮询方案每条命令额外等待 0 到 10ms。
            // 每条文本响应都受当前命令自己的 socket 超时保护。
            socket.setSoTimeout(toSocketTimeout(timeoutMs));
            try {
                String line = readLine(rawReader);
                if (line == null) {
                    return RootHelperResponse.error("RootDaemon 已关闭");
                }
                if (line.startsWith("OK\t")) {
                    return RootHelperResponse.ok(line.substring(3));
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
                return 0;
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

        private RootHelperResponse(boolean ok, String message) {
            this.ok = ok;
            this.message = message == null ? "" : message;
        }

        private static RootHelperResponse ok(String message) {
            return new RootHelperResponse(true, message);
        }

        private static RootHelperResponse error(String message) {
            return new RootHelperResponse(false, message);
        }
    }

    /**
     * Root shell 命令的传输结果。
     *
     * success 仅表示 RootDaemon 通信与命令进程启动成功，不代表命令的退出码为 0；这是
     * exec API 的既定语义，脚本应根据 output 内容自行判断。
     */
    public static final class ShellResult {
        public final boolean success;
        public final String output;
        public final String error;

        private ShellResult(boolean success, String output, String error) {
            this.success = success;
            this.output = output == null ? "" : output;
            this.error = error == null ? "" : error;
        }

        private static ShellResult success(String output) {
            return new ShellResult(true, output, "");
        }

        private static ShellResult failure(String error) {
            return new ShellResult(false, "", error);
        }
    }
}
