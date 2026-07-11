/**
 * 文件用途：RootDaemon 特权进程入口，持续接收本应用已认证客户端的 Root 命令。
 */
package com.autolua.engine;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;

/**
 * 由 `su -c app_process` 启动的常驻 RootDaemon。
 *
 * 它只监听 127.0.0.1，并要求随机令牌认证。App 主进程拥有它的生命周期；:engine 被强停后
 * 可以重新建立 socket 会话，但不会再次执行 su。截图仍使用“文本头 + 原始 RGBA”流，避免
 * 经过 Binder 或 Base64 造成整帧复制。
 */
public final class RootDaemonMain {
    private static volatile boolean running = true;
    private static ServerSocket serverSocket;
    private static String daemonToken;
    private static int ownerProcessId;

    private RootDaemonMain() {
    }

    public static void main(String[] args) {
        DaemonArguments arguments = DaemonArguments.parse(args);
        if (arguments == null) {
            return;
        }

        daemonToken = arguments.token;
        ownerProcessId = arguments.ownerProcessId;
        startOwnerWatchdog();

        try (ServerSocket server = new ServerSocket()) {
            serverSocket = server;
            server.setReuseAddress(true);
            server.bind(new InetSocketAddress(InetAddress.getByName("127.0.0.1"), arguments.port));

            while (running) {
                try {
                    Socket client = server.accept();
                    Thread thread = new Thread(
                            () -> handleClient(client),
                            "RootDaemonClient"
                    );
                    // main accept loop 结束后，客户端读阻塞不应让特权进程滞留。
                    thread.setDaemon(true);
                    thread.start();
                } catch (IOException exception) {
                    if (running) {
                        requestShutdown();
                    }
                }
            }
        } catch (IOException ignored) {
            // 端口无法绑定或系统关闭 socket 时不向 stdout 写入，避免污染二进制协议。
        } finally {
            serverSocket = null;
        }
    }

    /**
     * 处理一条独立客户端会话。认证超时只作用于首行；认证成功后连接可长期复用。
     */
    private static void handleClient(Socket client) {
        try (Socket socket = client) {
            socket.setSoTimeout(RootDaemonProtocol.AUTH_TIMEOUT_MS);
            InputStream inputStream = socket.getInputStream();
            OutputStream outputStream = socket.getOutputStream();
            String authLine = RootDaemonProtocol.readLine(inputStream);
            if (!isAuthenticationLineValid(authLine)) {
                RootDaemonProtocol.writeLine(outputStream, "ERR\tauth failed");
                return;
            }

            RootDaemonProtocol.writeLine(outputStream, "OK\tauth");
            socket.setSoTimeout(0);

            String line;
            while (running && (line = RootDaemonProtocol.readLine(inputStream)) != null) {
                String[] parts = line.split("\t", -1);
                if (parts.length > 0 && RootDaemonProtocol.SHUTDOWN_COMMAND.equals(parts[0])) {
                    RootDaemonProtocol.writeLine(outputStream, "OK\tbye");
                    requestShutdown();
                    return;
                }

                if (!RootHelperMain.dispatchCommand(outputStream, parts)) {
                    return;
                }
            }
        } catch (IOException ignored) {
            // 引擎强停会关闭 socket；RootDaemon 保持运行，等待下一次 :engine 连接。
        } catch (Exception ignored) {
            // Root 命令错误通过协议返回；无法恢复的客户端错误只关闭当前连接。
        }
    }

    private static boolean isAuthenticationLineValid(String line) {
        if (line == null) {
            return false;
        }
        String[] parts = line.split("\t", -1);
        if (parts.length != 2 || !RootDaemonProtocol.AUTH_COMMAND.equals(parts[0])) {
            return false;
        }
        return MessageDigest.isEqual(
                daemonToken.getBytes(StandardCharsets.UTF_8),
                parts[1].getBytes(StandardCharsets.UTF_8)
        );
    }

    /**
     * App 主进程被系统或用户杀掉时，特权 daemon 也必须主动退出，不能遗留 Root 服务。
     */
    private static void startOwnerWatchdog() {
        if (ownerProcessId <= 0) {
            return;
        }

        Thread watchdog = new Thread(() -> {
            while (running) {
                try {
                    Thread.sleep(2000L);
                } catch (InterruptedException exception) {
                    Thread.currentThread().interrupt();
                    return;
                }

                if (!new File("/proc/" + ownerProcessId).exists()) {
                    requestShutdown();
                    return;
                }
            }
        }, "RootDaemonOwnerWatchdog");
        watchdog.setDaemon(true);
        watchdog.start();
    }

    private static synchronized void requestShutdown() {
        if (!running) {
            return;
        }
        running = false;
        if (serverSocket != null) {
            try {
                serverSocket.close();
            } catch (IOException ignored) {
                // accept 已经结束时无需重复处理。
            }
        }
    }

    /**
     * RootDaemon 启动参数。
     */
    private static final class DaemonArguments {
        private final int port;
        private final String token;
        private final int ownerProcessId;

        private DaemonArguments(int port, String token, int ownerProcessId) {
            this.port = port;
            this.token = token;
            this.ownerProcessId = ownerProcessId;
        }

        private static DaemonArguments parse(String[] args) {
            int port = -1;
            String token = null;
            int ownerProcessId = -1;
            for (int index = 0; args != null && index < args.length; index++) {
                String argument = args[index];
                if ("--port".equals(argument) && index + 1 < args.length) {
                    port = parseInt(args[++index], -1);
                } else if ("--token".equals(argument) && index + 1 < args.length) {
                    token = args[++index];
                } else if ("--owner-pid".equals(argument) && index + 1 < args.length) {
                    ownerProcessId = parseInt(args[++index], -1);
                }
            }

            if (port != RootDaemonProtocol.PORT || !RootDaemonClient.isValidToken(token)) {
                return null;
            }
            return new DaemonArguments(port, token, ownerProcessId);
        }

        private static int parseInt(String value, int defaultValue) {
            try {
                return Integer.parseInt(value);
            } catch (NumberFormatException exception) {
                return defaultValue;
            }
        }
    }
}
