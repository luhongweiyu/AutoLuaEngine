/**
 * 文件用途：管理 Root 授权状态和常驻 Root shell 基础进程。
 */
package com.autolua.engine;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.TimeUnit;

/**
 * Root 运行层桥。
 *
 * Root 模式开启时先调用 prepareRootRuntime() 完成一次授权和常驻 su shell 创建。
 * 后续高频能力由 RootHelperBridge 的常驻 app_process 承接，不再暴露旧的
 * 任意命令、文件、进程、设备、应用控制入口。
 */
public final class RootShellBridge {
    private static final long ROOT_PROBE_TIMEOUT_MS = 2500;
    private static final long ROOT_CACHE_TRUE_MS = 60_000;
    private static final long ROOT_CACHE_FALSE_MS = 3_000;

    private static Boolean cachedRootAvailable;
    private static long rootCacheExpireAt;
    private static RootCommandMode cachedRootCommandMode = RootCommandMode.UNKNOWN;
    private static String cachedRootSuPath = "su";
    private static String lastRootError = "尚未检测 Root 权限";
    private static List<RootStatus.ProbeAttempt> lastProbeAttempts = Collections.emptyList();
    private static boolean rootRuntimePrepared;
    private static boolean rootRuntimeAvailable;
    private static RootShellSession rootShellSession;

    private RootShellBridge() {
    }

    public static boolean isRootAvailable() {
        if (isRootRuntimeReady()) {
            return true;
        }

        long now = System.currentTimeMillis();
        if (cachedRootAvailable != null && now < rootCacheExpireAt) {
            return cachedRootAvailable;
        }

        return prepareRootRuntime().available;
    }

    public static RootStatus status() {
        long now = System.currentTimeMillis();
        boolean cached = cachedRootAvailable != null && now < rootCacheExpireAt;
        boolean available = rootRuntimePrepared
                ? isRootRuntimeReady()
                : cached && Boolean.TRUE.equals(cachedRootAvailable);

        return new RootStatus(
                available,
                cachedRootCommandMode.name(),
                cachedRootSuPath,
                cached,
                rootCacheExpireAt,
                available ? "" : lastRootError,
                lastProbeAttempts
        );
    }

    public static synchronized RootStatus prepareRootRuntime() {
        if (isRootRuntimeReady()) {
            return status();
        }

        RootCommandMode rootCommandMode = detectRootCommandMode();
        boolean available = rootCommandMode != RootCommandMode.NONE;
        long now = System.currentTimeMillis();
        cachedRootAvailable = available;
        cachedRootCommandMode = rootCommandMode;
        cachedRootSuPath = rootCommandMode.suPath;
        rootCacheExpireAt = now + (available ? ROOT_CACHE_TRUE_MS : ROOT_CACHE_FALSE_MS);
        rootRuntimePrepared = true;

        if (!available) {
            closeRootShellSession();
            rootRuntimeAvailable = false;
            return status();
        }

        try {
            closeRootShellSession();
            rootShellSession = RootShellSession.start(rootCommandMode);
            rootRuntimeAvailable = rootShellSession.isAlive();
        } catch (IOException exception) {
            closeRootShellSession();
            rootRuntimeAvailable = false;
            lastRootError = exception.getMessage() == null
                    ? "启动 Root 运行层失败"
                    : exception.getMessage();
        }

        return status();
    }

    public static synchronized boolean isRootRuntimeReady() {
        rootRuntimeAvailable = rootRuntimePrepared
                && rootShellSession != null
                && rootShellSession.isAlive();
        return rootRuntimeAvailable;
    }

    public static synchronized void shutdown() {
        closeRootShellSession();
        rootRuntimePrepared = false;
        rootRuntimeAvailable = false;
        cachedRootAvailable = null;
        rootCacheExpireAt = 0L;
        cachedRootCommandMode = RootCommandMode.UNKNOWN;
        cachedRootSuPath = "su";
        lastRootError = "Root 运行层已关闭";
        lastProbeAttempts = Collections.emptyList();
    }

    static String shellQuote(String value) {
        return "'" + value.replace("'", "'\\''") + "'";
    }

    private static RootCommandMode detectRootCommandMode() {
        RootCommandMode mode = RootCommandMode.SU_C;
        CommandResult result = runCommand(mode.buildArgs("id -u"), ROOT_PROBE_TIMEOUT_MS);
        List<RootStatus.ProbeAttempt> attempts =
                Collections.singletonList(makeProbeAttempt(mode, result));
        lastProbeAttempts = attempts;

        if (result.exitCode == 0 && isRootIdentityOutput(result.stdoutText())) {
            lastRootError = "";
            return mode;
        }

        lastRootError = resolveProbeError(attempts);
        return RootCommandMode.NONE;
    }

    private static RootStatus.ProbeAttempt makeProbeAttempt(
            RootCommandMode mode,
            CommandResult result) {
        String error = "";
        if (result.timedOut) {
            error = "Root 探测超时";
        } else if (result.exitCode < 0) {
            error = result.stderrText().isEmpty() ? "Root 探测失败" : result.stderrText();
        }

        return new RootStatus.ProbeAttempt(
                mode.name(),
                mode.suPath,
                result.exitCode,
                result.stdoutText(),
                result.stderrText(),
                result.timedOut,
                error
        );
    }

    private static String resolveProbeError(List<RootStatus.ProbeAttempt> attempts) {
        if (attempts.isEmpty()) {
            return "Root 探测未执行";
        }

        RootStatus.ProbeAttempt attempt = attempts.get(attempts.size() - 1);
        String combined = (attempt.error + "\n" + attempt.stderr).toLowerCase();
        if (combined.contains("permission denied")) {
            return "App 进程未获得 Root 授权";
        }
        if (!attempt.error.isEmpty()) {
            return attempt.error;
        }
        if (!attempt.stderr.isEmpty()) {
            return attempt.stderr;
        }
        if (!attempt.stdout.isEmpty()) {
            return "Root 探测未返回 uid 0：" + attempt.stdout.trim();
        }
        return "Root 权限不可用";
    }

    private static boolean isRootIdentityOutput(String text) {
        if (text == null) {
            return false;
        }

        String value = text.trim();
        return "0".equals(value)
                || value.startsWith("uid=0(")
                || value.startsWith("uid=0 ");
    }

    private static synchronized void closeRootShellSession() {
        if (rootShellSession != null) {
            rootShellSession.close();
            rootShellSession = null;
        }
    }

    private static CommandResult runCommand(String[] args, long timeoutMs) {
        Process process = null;
        try {
            process = new ProcessBuilder(args).start();
            StreamCollector stdoutCollector = new StreamCollector(process.getInputStream());
            StreamCollector stderrCollector = new StreamCollector(process.getErrorStream());
            Thread stdoutThread = new Thread(stdoutCollector, "RootProbeStdout");
            Thread stderrThread = new Thread(stderrCollector, "RootProbeStderr");
            stdoutThread.start();
            stderrThread.start();

            if (!waitForProcess(process, timeoutMs)) {
                process.destroy();
                joinCollector(stdoutThread);
                joinCollector(stderrThread);
                return CommandResult.timeout();
            }

            joinCollector(stdoutThread);
            joinCollector(stderrThread);
            return new CommandResult(
                    process.exitValue(),
                    stdoutCollector.toByteArray(),
                    stderrCollector.toByteArray(),
                    false
            );
        } catch (IOException exception) {
            return CommandResult.failure(exception.getMessage());
        } finally {
            if (process != null) {
                process.destroy();
            }
        }
    }

    private static boolean waitForProcess(Process process, long timeoutMs) {
        long deadline = System.currentTimeMillis() + timeoutMs;
        while (System.currentTimeMillis() < deadline) {
            try {
                process.exitValue();
                return true;
            } catch (IllegalThreadStateException ignored) {
                try {
                    TimeUnit.MILLISECONDS.sleep(20);
                } catch (InterruptedException exception) {
                    Thread.currentThread().interrupt();
                    return false;
                }
            }
        }
        return false;
    }

    private static void joinCollector(Thread thread) {
        try {
            thread.join(200);
        } catch (InterruptedException exception) {
            Thread.currentThread().interrupt();
        }
    }

    private static final class CommandResult {
        private final int exitCode;
        private final byte[] stdout;
        private final byte[] stderr;
        private final boolean timedOut;

        private CommandResult(int exitCode, byte[] stdout, byte[] stderr, boolean timedOut) {
            this.exitCode = exitCode;
            this.stdout = stdout == null ? new byte[0] : stdout;
            this.stderr = stderr == null ? new byte[0] : stderr;
            this.timedOut = timedOut;
        }

        private static CommandResult failure(String error) {
            return new CommandResult(-1, new byte[0], stringToBytes(error), false);
        }

        private static CommandResult timeout() {
            return new CommandResult(-1, new byte[0], stringToBytes("Root 命令超时"), true);
        }

        private static byte[] stringToBytes(String text) {
            if (text == null || text.isEmpty()) {
                return new byte[0];
            }
            return text.getBytes(StandardCharsets.UTF_8);
        }

        private String stdoutText() {
            return new String(stdout, StandardCharsets.UTF_8);
        }

        private String stderrText() {
            return new String(stderr, StandardCharsets.UTF_8);
        }
    }

    /**
     * 读取进程输出，避免 su 探测命令输出阻塞。
     */
    private static final class StreamCollector implements Runnable {
        private final InputStream inputStream;
        private final ByteArrayOutputStream outputStream = new ByteArrayOutputStream();

        private StreamCollector(InputStream inputStream) {
            this.inputStream = inputStream;
        }

        @Override
        public void run() {
            try (InputStream source = inputStream) {
                byte[] buffer = new byte[4096];
                int readCount;
                while ((readCount = source.read(buffer)) != -1) {
                    outputStream.write(buffer, 0, readCount);
                }
            } catch (IOException ignored) {
                // 进程结束或超时时流可能被关闭，结果由 exitCode 判断。
            }
        }

        private byte[] toByteArray() {
            return outputStream.toByteArray();
        }
    }

    /**
     * 常驻 su shell 基础进程。
     *
     * 当前不对外执行任意命令，只用于保持 Root 授权会话和运行层状态。
     */
    private static final class RootShellSession {
        private final Process process;
        private final OutputStream stdin;

        private RootShellSession(Process process) {
            this.process = process;
            this.stdin = process.getOutputStream();
            startDrainer(process.getInputStream(), "RootShellStdout");
            startDrainer(process.getErrorStream(), "RootShellStderr");
        }

        private static RootShellSession start(RootCommandMode mode) throws IOException {
            return new RootShellSession(new ProcessBuilder(mode.buildInteractiveArgs()).start());
        }

        private boolean isAlive() {
            try {
                process.exitValue();
                return false;
            } catch (IllegalThreadStateException ignored) {
                return true;
            }
        }

        private void close() {
            try {
                stdin.write("exit\n".getBytes(StandardCharsets.UTF_8));
                stdin.flush();
            } catch (IOException ignored) {
                // shell 可能已经退出，继续 destroy。
            }
            process.destroy();
        }

        private void startDrainer(InputStream inputStream, String name) {
            Thread thread = new Thread(() -> {
                try (InputStream source = inputStream) {
                    byte[] buffer = new byte[1024];
                    while (source.read(buffer) != -1) {
                        // 只排空管道，避免常驻 shell 因输出阻塞。
                    }
                } catch (IOException ignored) {
                    // shell 退出时自然结束。
                }
            }, name);
            thread.setDaemon(true);
            thread.start();
        }
    }

    private enum RootCommandMode {
        UNKNOWN("su"),
        NONE(""),
        SU_C("su");

        private final String suPath;

        RootCommandMode(String suPath) {
            this.suPath = suPath;
        }

        private String[] buildArgs(String command) {
            String executable = suPath.isEmpty() ? "su" : suPath;
            return new String[]{executable, "-c", command};
        }

        private String[] buildInteractiveArgs() {
            String executable = suPath.isEmpty() ? "su" : suPath;
            return new String[]{executable, "-c", "sh"};
        }
    }
}
