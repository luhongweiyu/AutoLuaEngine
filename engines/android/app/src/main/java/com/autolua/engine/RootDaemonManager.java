/**
 * 文件用途：在 App 主进程启动、检查和关闭 RootDaemon，不让 :engine 持有 su 会话。
 */
package com.autolua.engine;

import android.content.Context;
import android.util.Base64;

import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.security.SecureRandom;

/**
 * RootDaemon 生命周期管理器。
 *
 * Root 授权只在主进程初始化 daemon 时发生。引擎进程后续只建立本地 socket 连接，因此“强停
 * 引擎进程”不会结束 Root 会话，也不会在下一次脚本运行时重新触发 su 授权提示。
 */
final class RootDaemonManager {
    private static final Object LOCK = new Object();
    private static final long START_TIMEOUT_MS = 3500L;
    private static final SecureRandom RANDOM = new SecureRandom();

    private static Process daemonProcess;
    private static String lastError = "RootDaemon 尚未启动";

    private RootDaemonManager() {
    }

    static boolean prepare(Context context) {
        Context appContext = context == null ? null : context.getApplicationContext();
        if (appContext == null || !EngineSettings.isRootModeEnabled(appContext)) {
            return false;
        }

        synchronized (LOCK) {
            String previousToken = RootDaemonClient.readToken(appContext);
            if (previousToken != null && RootDaemonClient.isReady(previousToken, RootDaemonProtocol.CONNECT_TIMEOUT_MS)) {
                lastError = "";
                return true;
            }

            // 使用旧令牌尽力关闭同一 App 遗留的 daemon，再生成新令牌启动新的特权进程。
            RootDaemonClient.requestShutdown(previousToken);
            closeProcessLocked();

            String token = createToken();
            if (!writeToken(appContext, token)) {
                lastError = "写入 RootDaemon 认证令牌失败";
                return false;
            }

            try {
                daemonProcess = startDaemonProcess(appContext, token);
            } catch (IOException exception) {
                lastError = exception.getMessage() == null
                        ? "启动 RootDaemon 失败"
                        : exception.getMessage();
                closeProcessLocked();
                return false;
            }

            long deadline = System.currentTimeMillis() + START_TIMEOUT_MS;
            while (System.currentTimeMillis() < deadline) {
                if (RootDaemonClient.isReady(token, RootDaemonProtocol.CONNECT_TIMEOUT_MS)) {
                    lastError = "";
                    return true;
                }
                if (!isProcessAlive(daemonProcess)) {
                    break;
                }
                try {
                    Thread.sleep(40L);
                } catch (InterruptedException exception) {
                    Thread.currentThread().interrupt();
                    break;
                }
            }

            lastError = "RootDaemon 未在限定时间内就绪";
            closeProcessLocked();
            return false;
        }
    }

    static void shutdown(Context context) {
        Context appContext = context == null ? null : context.getApplicationContext();
        synchronized (LOCK) {
            RootDaemonClient.requestShutdown(RootDaemonClient.readToken(appContext));
            closeProcessLocked();
            deleteToken(appContext);
            lastError = "RootDaemon 已关闭";
        }
    }

    static String lastError() {
        synchronized (LOCK) {
            return lastError;
        }
    }

    private static Process startDaemonProcess(Context context, String token) throws IOException {
        String classPath = context.getPackageCodePath();
        String command = "CLASSPATH="
                + shellQuote(classPath)
                + " app_process /system/bin com.autolua.engine.RootDaemonMain"
                + " --port " + RootDaemonProtocol.PORT
                + " --token " + token
                + " --owner-pid " + android.os.Process.myPid();
        Process process = new ProcessBuilder("su", "-c", command).start();
        drainProcessStream(process.getInputStream(), "RootDaemonStdout");
        drainProcessStream(process.getErrorStream(), "RootDaemonStderr");
        return process;
    }

    private static String createToken() {
        byte[] bytes = new byte[32];
        RANDOM.nextBytes(bytes);
        return Base64.encodeToString(
                bytes,
                Base64.URL_SAFE | Base64.NO_PADDING | Base64.NO_WRAP
        );
    }

    private static boolean writeToken(Context context, String token) {
        if (context == null || !RootDaemonClient.isValidToken(token)) {
            return false;
        }

        try (FileOutputStream outputStream = context.openFileOutput(
                RootDaemonProtocol.TOKEN_FILE_NAME,
                Context.MODE_PRIVATE
        )) {
            outputStream.write(token.getBytes(java.nio.charset.StandardCharsets.UTF_8));
            outputStream.flush();
            return true;
        } catch (IOException exception) {
            return false;
        }
    }

    private static void deleteToken(Context context) {
        if (context != null) {
            context.deleteFile(RootDaemonProtocol.TOKEN_FILE_NAME);
        }
    }

    private static void closeProcessLocked() {
        if (daemonProcess != null) {
            daemonProcess.destroy();
            daemonProcess = null;
        }
    }

    private static boolean isProcessAlive(Process process) {
        if (process == null) {
            return false;
        }
        try {
            process.exitValue();
            return false;
        } catch (IllegalThreadStateException ignored) {
            return true;
        }
    }

    private static void drainProcessStream(InputStream inputStream, String threadName) {
        Thread thread = new Thread(() -> {
            try (InputStream source = inputStream) {
                byte[] buffer = new byte[1024];
                while (source.read(buffer) != -1) {
                    // RootDaemon 不使用 stdout/stderr 协议，只排空管道避免子进程阻塞。
                }
            } catch (IOException ignored) {
                // daemon 结束时流自然关闭。
            }
        }, threadName);
        thread.setDaemon(true);
        thread.start();
    }

    private static String shellQuote(String value) {
        return "'" + value.replace("'", "'\\''") + "'";
    }
}
