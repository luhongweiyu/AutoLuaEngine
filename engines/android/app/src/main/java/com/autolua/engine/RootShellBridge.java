package com.autolua.engine;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.TimeUnit;

/**
 * Root shell 能力桥。
 *
 * 第一版 root 模式不做常驻守护进程，先通过 su 执行短命令，覆盖点击、滑动
 * 和系统按键这类高优先级能力。不同设备的 su 参数格式不完全一致，所以这里
 * 会先探测可用格式并缓存；后续需要高频能力时，再评估常驻 root service
 * 或本地 socket，避免每次命令都创建进程。
 */
public final class RootShellBridge {
    private static final long DEFAULT_TIMEOUT_MS = 2500;
    private static final int MAX_COMMAND_TIMEOUT_MS = 30_000;
    private static final long ROOT_CACHE_TRUE_MS = 60_000;
    private static final long ROOT_CACHE_FALSE_MS = 3_000;

    private static Boolean cachedRootAvailable;
    private static long rootCacheExpireAt;
    private static RootCommandMode cachedRootCommandMode = RootCommandMode.UNKNOWN;

    private RootShellBridge() {
    }

    public static boolean isRootAvailable() {
        long now = System.currentTimeMillis();
        if (cachedRootAvailable != null && now < rootCacheExpireAt) {
            return cachedRootAvailable;
        }

        RootCommandMode rootCommandMode = detectRootCommandMode();
        boolean available = rootCommandMode != RootCommandMode.NONE;
        cachedRootAvailable = available;
        cachedRootCommandMode = rootCommandMode;
        rootCacheExpireAt = now + (available ? ROOT_CACHE_TRUE_MS : ROOT_CACHE_FALSE_MS);
        return available;
    }

    public static RootCommandResult exec(String command, int timeoutMs) {
        if (command == null || command.trim().isEmpty()) {
            return RootCommandResult.failure("root command is required");
        }

        int safeTimeoutMs = normalizeTimeoutMs(timeoutMs);
        CommandResult result = runRootCommand(command, safeTimeoutMs);
        return RootCommandResult.fromCommandResult(result);
    }

    public static boolean tap(int x, int y) {
        if (!isRootAvailable()) {
            return false;
        }
        return runInputCommand("tap " + x + " " + y);
    }

    public static boolean swipe(int x1, int y1, int x2, int y2, int durationMs) {
        if (!isRootAvailable()) {
            return false;
        }
        int safeDuration = Math.max(durationMs, 1);
        return runInputCommand("swipe "
                + x1 + " "
                + y1 + " "
                + x2 + " "
                + y2 + " "
                + safeDuration);
    }

    public static boolean keyBack() {
        return keyEvent(4);
    }

    public static boolean keyHome() {
        return keyEvent(3);
    }

    private static boolean keyEvent(int keyCode) {
        if (!isRootAvailable()) {
            return false;
        }
        return runInputCommand("keyevent " + keyCode);
    }

    private static boolean runInputCommand(String inputArgs) {
        CommandResult result = runRootCommand("input " + inputArgs, DEFAULT_TIMEOUT_MS);
        return result.exitCode == 0;
    }

    private static RootCommandMode detectRootCommandMode() {
        for (RootCommandMode mode : RootCommandMode.PROBE_ORDER) {
            CommandResult result = runCommand(mode.buildArgs("id -u"), DEFAULT_TIMEOUT_MS);
            if (result.exitCode == 0 && "0".equals(result.stdoutText().trim())) {
                return mode;
            }
        }
        return RootCommandMode.NONE;
    }

    static CommandResult runRootCommand(String command, long timeoutMs) {
        if (!isRootAvailable()) {
            return CommandResult.failure("root is not available");
        }
        return runCommand(cachedRootCommandMode.buildArgs(command), timeoutMs);
    }

    private static int normalizeTimeoutMs(int timeoutMs) {
        if (timeoutMs <= 0) {
            return (int) DEFAULT_TIMEOUT_MS;
        }
        return Math.min(timeoutMs, MAX_COMMAND_TIMEOUT_MS);
    }

    private static CommandResult runCommand(String[] args, long timeoutMs) {
        Process process = null;
        try {
            process = new ProcessBuilder(args).start();
            StreamCollector stdoutCollector = new StreamCollector(process.getInputStream());
            StreamCollector stderrCollector = new StreamCollector(process.getErrorStream());
            Thread stdoutThread = new Thread(stdoutCollector, "RootShellStdout");
            Thread stderrThread = new Thread(stderrCollector, "RootShellStderr");
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
                    stderrCollector.toByteArray()
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

    static final class CommandResult {
        final int exitCode;
        final byte[] stdout;
        final byte[] stderr;
        final boolean timedOut;

        private CommandResult(int exitCode, byte[] stdout, byte[] stderr) {
            this(
                    exitCode,
                    stdout,
                    stderr,
                    false
            );
        }

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
            return new CommandResult(-1, new byte[0], stringToBytes("root command timeout"), true);
        }

        private static byte[] stringToBytes(String text) {
            if (text == null || text.isEmpty()) {
                return new byte[0];
            }
            return text.getBytes(StandardCharsets.UTF_8);
        }

        String stdoutText() {
            return new String(stdout, StandardCharsets.UTF_8);
        }

        String stderrText() {
            return new String(stderr, StandardCharsets.UTF_8);
        }
    }

    /**
     * 独立线程读取进程输出，避免 `screencap` 这类大 stdout 命令把管道写满后卡死。
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
                byte[] buffer = new byte[16 * 1024];
                int readCount;
                while ((readCount = source.read(buffer)) != -1) {
                    outputStream.write(buffer, 0, readCount);
                }
            } catch (IOException ignored) {
                // 命令超时或进程退出时流可能被关闭；调用方会根据 exitCode 判断结果。
            }
        }

        private byte[] toByteArray() {
            return outputStream.toByteArray();
        }
    }

    private enum RootCommandMode {
        UNKNOWN,
        NONE,
        SU_C,
        SU_0_SH_C,
        SU_ROOT_SH_C;

        private static final RootCommandMode[] PROBE_ORDER = {
                SU_C,
                SU_0_SH_C,
                SU_ROOT_SH_C,
        };

        private String[] buildArgs(String command) {
            switch (this) {
                case SU_C:
                    return new String[]{"su", "-c", command};
                case SU_0_SH_C:
                    return new String[]{"su", "0", "sh", "-c", command};
                case SU_ROOT_SH_C:
                    return new String[]{"su", "root", "sh", "-c", command};
                default:
                    return new String[]{"su", "-c", command};
            }
        }
    }
}
