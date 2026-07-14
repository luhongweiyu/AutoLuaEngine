/**
 * 文件用途：引擎和 App 主进程访问 RootDaemon 的认证连接与状态查询入口。
 */
package com.xiaoyv.engine;

import android.content.Context;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.Collections;

/**
 * RootDaemon 客户端。
 *
 * :engine 进程只通过此类连接 App 主进程提前启动的 RootDaemon，绝不在脚本运行、截图或
 * 输入命令路径中拉起 su。认证令牌存放在 App 私有目录，其他应用无法读取。
 */
final class RootDaemonClient {
    private RootDaemonClient() {
    }

    static Socket openAuthenticatedSocket(Context context, int timeoutMs) throws IOException {
        String token = readToken(context);
        if (token == null) {
            throw new IOException("RootDaemon 认证令牌不存在");
        }
        return openAuthenticatedSocket(token, RootDaemonProtocol.port(context), timeoutMs);
    }

    static boolean isReady(Context context) {
        String token = readToken(context);
        return token != null && isReady(
                context,
                token,
                RootDaemonProtocol.CONNECT_TIMEOUT_MS
        );
    }

    static boolean isReady(Context context, String token, int timeoutMs) {
        try (Socket socket = openAuthenticatedSocket(
                token,
                RootDaemonProtocol.port(context),
                timeoutMs
        )) {
            RootDaemonProtocol.writeLine(socket.getOutputStream(), "ping");
            return RootDaemonProtocol.isOk(
                    RootDaemonProtocol.readLine(socket.getInputStream()),
                    "pong"
            );
        } catch (IOException exception) {
            return false;
        }
    }

    /**
     * 判断现有 daemon 是否确实属于当前 App 主进程。
     *
     * APK 覆盖安装或主进程重启时，旧 daemon 可能在 owner watchdog 检查前短暂响应 ping；
     * 只有 owner PID 一致才能复用，避免刚建立音量键订阅后旧 daemon 随即退出。
     */
    static boolean isOwnedBy(
            Context context,
            String token,
            int expectedOwnerPid,
            int timeoutMs
    ) {
        if (expectedOwnerPid <= 0) {
            return false;
        }
        try (Socket socket = openAuthenticatedSocket(
                token,
                RootDaemonProtocol.port(context),
                timeoutMs
        )) {
            RootDaemonProtocol.writeLine(
                    socket.getOutputStream(),
                    RootDaemonProtocol.OWNER_PID_COMMAND
            );
            String response = RootDaemonProtocol.readLine(socket.getInputStream());
            return ("OK\townerPid\t" + expectedOwnerPid).equals(response);
        } catch (IOException exception) {
            return false;
        }
    }

    static boolean requestShutdown(Context context, String token) {
        if (token == null || token.isEmpty()) {
            return false;
        }

        try (Socket socket = openAuthenticatedSocket(
                token,
                RootDaemonProtocol.port(context),
                RootDaemonProtocol.CONNECT_TIMEOUT_MS
        )) {
            RootDaemonProtocol.writeLine(
                    socket.getOutputStream(),
                    RootDaemonProtocol.SHUTDOWN_COMMAND
            );
            return RootDaemonProtocol.isOk(
                    RootDaemonProtocol.readLine(socket.getInputStream()),
                    "bye"
            );
        } catch (IOException exception) {
            return false;
        }
    }

    static RootStatus status(Context context) {
        if (context == null) {
            return unavailableStatus("Application Context 不可用");
        }
        if (!EngineSettings.isRootModeEnabled(context)) {
            return unavailableStatus("Root 模式未开启");
        }
        return isReady(context)
                ? new RootStatus(
                        true,
                        "ROOT_DAEMON",
                        "su",
                        false,
                        0L,
                        "",
                        Collections.emptyList()
                )
                : unavailableStatus("RootDaemon 未就绪");
    }

    static String readToken(Context context) {
        if (context == null) {
            return null;
        }

        File tokenFile = new File(context.getFilesDir(), RootDaemonProtocol.TOKEN_FILE_NAME);
        if (!tokenFile.isFile() || tokenFile.length() <= 0 || tokenFile.length() > 256) {
            return null;
        }

        try (FileInputStream inputStream = new FileInputStream(tokenFile)) {
            byte[] bytes = new byte[(int) tokenFile.length()];
            int offset = 0;
            while (offset < bytes.length) {
                int count = inputStream.read(bytes, offset, bytes.length - offset);
                if (count < 0) {
                    return null;
                }
                offset += count;
            }
            String token = new String(bytes, StandardCharsets.UTF_8).trim();
            return isValidToken(token) ? token : null;
        } catch (IOException exception) {
            return null;
        }
    }

    static boolean isValidToken(String token) {
        if (token == null || token.length() < 32 || token.length() > 128) {
            return false;
        }
        for (int index = 0; index < token.length(); index++) {
            char value = token.charAt(index);
            if (!(value >= 'a' && value <= 'z')
                    && !(value >= 'A' && value <= 'Z')
                    && !(value >= '0' && value <= '9')
                    && value != '-'
                    && value != '_') {
                return false;
            }
        }
        return true;
    }

    private static Socket openAuthenticatedSocket(String token, int port, int timeoutMs) throws IOException {
        if (!isValidToken(token) || !RootDaemonProtocol.isDaemonPort(port)) {
            throw new IOException("RootDaemon 认证令牌无效");
        }

        Socket socket = new Socket();
        try {
            socket.connect(
                    new InetSocketAddress(InetAddress.getByName("127.0.0.1"), port),
                    timeoutMs
            );
            socket.setSoTimeout(Math.max(timeoutMs, RootDaemonProtocol.AUTH_TIMEOUT_MS));
            RootDaemonProtocol.writeLine(
                    socket.getOutputStream(),
                    RootDaemonProtocol.AUTH_COMMAND + "\t" + token
            );
            if (!RootDaemonProtocol.isOk(
                    RootDaemonProtocol.readLine(socket.getInputStream()),
                    "auth"
            )) {
                throw new IOException("RootDaemon 认证失败");
            }
            socket.setSoTimeout(0);
            return socket;
        } catch (IOException exception) {
            try {
                socket.close();
            } catch (IOException ignored) {
                // 认证失败后只需要关闭未建立完成的本地连接。
            }
            throw exception;
        }
    }

    private static RootStatus unavailableStatus(String error) {
        return new RootStatus(
                false,
                "ROOT_DAEMON",
                "su",
                false,
                0L,
                error,
                Collections.emptyList()
        );
    }
}
