package com.autolua.engine;

import java.io.ByteArrayOutputStream;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;

/**
 * Root shell 能力桥。
 *
 * Root shell 能力先通过 su 探测设备支持的参数格式。
 *
 * 点击、滑动和系统按键这类无输出命令会优先复用常驻 root shell，减少频繁
 * 创建 su 进程的开销；root.exec 和 screencap 这类需要准确 stdout/stderr 或
 * 二进制输出的命令仍使用短命令，避免输出边界混乱。
 */
public final class RootShellBridge {
    private static final long DEFAULT_TIMEOUT_MS = 2500;
    private static final int MAX_COMMAND_TIMEOUT_MS = 30_000;
    private static final long ROOT_CACHE_TRUE_MS = 60_000;
    private static final long ROOT_CACHE_FALSE_MS = 3_000;

    private static Boolean cachedRootAvailable;
    private static long rootCacheExpireAt;
    private static RootCommandMode cachedRootCommandMode = RootCommandMode.UNKNOWN;
    private static RootShellSession rootShellSession;

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

    public static boolean keyEvent(int keyCode) {
        if (!isRootAvailable()) {
            return false;
        }
        return runInputCommand("keyevent " + keyCode);
    }

    /**
     * 使用 Android input text 做第一版文本输入。
     *
     * Android input 命令把空格写成 %s；换行和复杂中文在不同系统上表现不稳定，
     * 这里先明确拒绝，后续改为输入法或剪贴板方案时再扩展。
     */
    public static boolean inputText(String text) {
        if (!isRootAvailable() || text == null || text.indexOf('\n') >= 0 || text.indexOf('\r') >= 0) {
            return false;
        }
        return runInputCommand("text " + shellQuote(text.replace(" ", "%s")));
    }

    private static boolean runInputCommand(String inputArgs) {
        String command = "input " + inputArgs;
        CommandResult result = runPersistentRootCommand(command, DEFAULT_TIMEOUT_MS);
        if (result.exitCode != 0) {
            result = runRootCommand(command, DEFAULT_TIMEOUT_MS);
        }
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

    private static synchronized CommandResult runPersistentRootCommand(String command, long timeoutMs) {
        if (!isRootAvailable()) {
            return CommandResult.failure("root is not available");
        }

        try {
            if (rootShellSession == null
                    || !rootShellSession.isForMode(cachedRootCommandMode)
                    || !rootShellSession.isAlive()) {
                closeRootShellSession();
                rootShellSession = RootShellSession.start(cachedRootCommandMode);
            }

            CommandResult result = rootShellSession.runNoOutputCommand(command, timeoutMs);
            if (result.timedOut || result.exitCode < 0) {
                closeRootShellSession();
            }
            return result;
        } catch (IOException exception) {
            closeRootShellSession();
            return CommandResult.failure(exception.getMessage());
        }
    }

    private static synchronized void closeRootShellSession() {
        if (rootShellSession != null) {
            rootShellSession.close();
            rootShellSession = null;
        }
    }

    private static int normalizeTimeoutMs(int timeoutMs) {
        if (timeoutMs <= 0) {
            return (int) DEFAULT_TIMEOUT_MS;
        }
        return Math.min(timeoutMs, MAX_COMMAND_TIMEOUT_MS);
    }

    private static String shellQuote(String value) {
        return "'" + value.replace("'", "'\\''") + "'";
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

    /**
     * 常驻 root shell。
     *
     * 这里只用于无输出命令：命令的 stdout/stderr 会被重定向到 /dev/null，然后
     * 额外打印一行标记读取退出码。这样可以安全处理 input tap/swipe/keyevent，
     * 又不会和 root.exec、screencap 的输出格式互相污染。
     */
    private static final class RootShellSession {
        private static final String MARKER_PREFIX = "__AEL_ROOT_RC_";

        private final RootCommandMode mode;
        private final Process process;
        private final OutputStream stdin;
        private final BlockingQueue<String> stdoutLines = new LinkedBlockingQueue<>();
        private int commandSequence;

        private RootShellSession(RootCommandMode mode, Process process) {
            this.mode = mode;
            this.process = process;
            this.stdin = process.getOutputStream();
            startStdoutReader(process.getInputStream());
            startStderrDrainer(process.getErrorStream());
        }

        private static RootShellSession start(RootCommandMode mode) throws IOException {
            return new RootShellSession(mode, new ProcessBuilder(mode.buildInteractiveArgs()).start());
        }

        private boolean isForMode(RootCommandMode currentMode) {
            return mode == currentMode;
        }

        private boolean isAlive() {
            try {
                process.exitValue();
                return false;
            } catch (IllegalThreadStateException ignored) {
                return true;
            }
        }

        private CommandResult runNoOutputCommand(String command, long timeoutMs) throws IOException {
            if (!isAlive()) {
                return CommandResult.failure("root shell is not alive");
            }

            String marker = MARKER_PREFIX
                    + System.currentTimeMillis()
                    + "_"
                    + (++commandSequence)
                    + "__";
            String script = command
                    + " >/dev/null 2>&1\n"
                    + "rc=$?\n"
                    + "printf '"
                    + marker
                    + ":%s\\n' \"$rc\"\n";
            stdin.write(script.getBytes(StandardCharsets.UTF_8));
            stdin.flush();

            long deadline = System.currentTimeMillis() + timeoutMs;
            while (System.currentTimeMillis() < deadline) {
                long waitMs = Math.max(1L, deadline - System.currentTimeMillis());
                String line;
                try {
                    line = stdoutLines.poll(Math.min(waitMs, 50L), TimeUnit.MILLISECONDS);
                } catch (InterruptedException exception) {
                    Thread.currentThread().interrupt();
                    return CommandResult.failure("root shell command interrupted");
                }

                if (line == null || !line.startsWith(marker + ":")) {
                    continue;
                }

                return new CommandResult(parseMarkerExitCode(line, marker), new byte[0], new byte[0]);
            }

            return CommandResult.timeout();
        }

        private int parseMarkerExitCode(String line, String marker) {
            String value = line.substring(marker.length() + 1).trim();
            try {
                return Integer.parseInt(value);
            } catch (NumberFormatException exception) {
                return -1;
            }
        }

        private void startStdoutReader(InputStream inputStream) {
            Thread thread = new Thread(() -> {
                try (BufferedReader reader = new BufferedReader(
                        new InputStreamReader(inputStream, StandardCharsets.UTF_8))) {
                    String line;
                    while ((line = reader.readLine()) != null) {
                        stdoutLines.offer(line);
                    }
                } catch (IOException ignored) {
                    // shell 关闭时读取线程自然结束。
                }
            }, "RootShellSessionStdout");
            thread.setDaemon(true);
            thread.start();
        }

        private void startStderrDrainer(InputStream inputStream) {
            Thread thread = new Thread(() -> {
                try (InputStream source = inputStream) {
                    byte[] buffer = new byte[1024];
                    while (source.read(buffer) != -1) {
                        // 这里只需要排空 stderr，避免 root shell 因管道写满而阻塞。
                    }
                } catch (IOException ignored) {
                    // shell 关闭时读取线程自然结束。
                }
            }, "RootShellSessionStderr");
            thread.setDaemon(true);
            thread.start();
        }

        private void close() {
            try {
                stdin.write("exit\n".getBytes(StandardCharsets.UTF_8));
                stdin.flush();
            } catch (IOException ignored) {
                // 进程可能已经退出，直接 destroy 即可。
            }
            process.destroy();
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

        private String[] buildInteractiveArgs() {
            switch (this) {
                case SU_C:
                    return new String[]{"su", "-c", "sh"};
                case SU_0_SH_C:
                    return new String[]{"su", "0", "sh"};
                case SU_ROOT_SH_C:
                    return new String[]{"su", "root", "sh"};
                default:
                    return new String[]{"su"};
            }
        }
    }
}
