package com.autolua.engine;

import java.io.ByteArrayOutputStream;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;

/**
 * Root shell 能力桥。
 *
 * Root 运行层固定使用一次 `su -c sh` 常驻会话。Root 模式开启、App 启动或
 * 切换运行模式时先调用 prepareRootRuntime()，后续命令只发给这条会话执行。
 * 这样点击、文件、设备控制、应用控制和 root.exec 都不会反复拉起 su 进程。
 *
 * 注意：二进制大输出不走常驻文本通道，旧 screencap 调试路径单独保留。
 */
public final class RootShellBridge {
    private static final long DEFAULT_TIMEOUT_MS = 2500;
    private static final int MAX_COMMAND_TIMEOUT_MS = 30_000;
    private static final long ROOT_CACHE_TRUE_MS = 60_000;
    private static final long ROOT_CACHE_FALSE_MS = 3_000;
    private static final int KEYCODE_PASTE = 279;
    private static final String TEXT_STDOUT_BEGIN = "__AEL_TEXT_STDOUT_BEGIN__";
    private static final String TEXT_STDOUT_END = "__AEL_TEXT_STDOUT_END__";
    private static final String TEXT_STDERR_BEGIN = "__AEL_TEXT_STDERR_BEGIN__";
    private static final String TEXT_STDERR_END = "__AEL_TEXT_STDERR_END__";

    private static Boolean cachedRootAvailable;
    private static long rootCacheExpireAt;
    private static RootCommandMode cachedRootCommandMode = RootCommandMode.UNKNOWN;
    private static String cachedRootSuPath = "su";
    private static String lastRootError = "root has not been probed";
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
        // 已经有可用的常驻 root shell 时直接复用，不再重复执行 su 探测。
        if (isRootRuntimeReady()) {
            return status();
        }

        long now = System.currentTimeMillis();
        RootCommandMode rootCommandMode = detectRootCommandMode();
        boolean available = rootCommandMode != RootCommandMode.NONE;
        cachedRootAvailable = available;
        cachedRootCommandMode = rootCommandMode;
        rootCacheExpireAt = now + (available ? ROOT_CACHE_TRUE_MS : ROOT_CACHE_FALSE_MS);
        rootRuntimePrepared = true;

        if (!available) {
            closeRootShellSession();
            rootRuntimeAvailable = false;
            return status();
        }

        try {
            if (rootShellSession == null
                    || !rootShellSession.isForMode(cachedRootCommandMode)
                    || !rootShellSession.isAlive()) {
                closeRootShellSession();
                rootShellSession = RootShellSession.start(cachedRootCommandMode);
            }
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
        return rootRuntimePrepared
                && rootRuntimeAvailable
                && rootShellSession != null
                && rootShellSession.isAlive();
    }

    public static synchronized void shutdown() {
        closeRootShellSession();
        rootRuntimePrepared = false;
        rootRuntimeAvailable = false;
        cachedRootAvailable = null;
        rootCacheExpireAt = 0L;
        cachedRootCommandMode = RootCommandMode.UNKNOWN;
        cachedRootSuPath = "su";
        lastRootError = "root runtime is stopped";
        lastProbeAttempts = Collections.emptyList();
    }

    public static RootCommandResult exec(String command, int timeoutMs) {
        if (command == null || command.trim().isEmpty()) {
            return RootCommandResult.failure("root command is required");
        }

        int safeTimeoutMs = normalizeTimeoutMs(timeoutMs);
        CommandResult result = runRootCommand(command, safeTimeoutMs);
        return RootCommandResult.fromCommandResult(result);
    }

    public static RootCommandResult fileExists(String path) {
        if (path == null || path.isEmpty()) {
            return RootCommandResult.failure("root file path is required");
        }
        return RootCommandResult.fromCommandResult(
                runRootCommand("[ -e " + shellQuote(path) + " ]", DEFAULT_TIMEOUT_MS)
        );
    }

    public static RootCommandResult readTextFile(String path, int timeoutMs) {
        if (path == null || path.isEmpty()) {
            return RootCommandResult.failure("root file path is required");
        }

        int safeTimeoutMs = normalizeTimeoutMs(timeoutMs);
        return RootCommandResult.fromCommandResult(
                runRootCommand("cat " + shellQuote(path), safeTimeoutMs)
        );
    }

    public static RootCommandResult writeTextFile(String path, String content, int timeoutMs) {
        if (path == null || path.isEmpty()) {
            return RootCommandResult.failure("root file path is required");
        }
        if (content == null) {
            return RootCommandResult.failure("root file content is required");
        }

        int safeTimeoutMs = normalizeTimeoutMs(timeoutMs);
        String base64 = android.util.Base64.encodeToString(
                content.getBytes(StandardCharsets.UTF_8),
                android.util.Base64.NO_WRAP
        );
        String command = "printf %s " + shellQuote(base64)
                + " | base64 -d > " + shellQuote(path);
        return RootCommandResult.fromCommandResult(runRootCommand(command, safeTimeoutMs));
    }

    public static RootCommandResult statFile(String path) {
        if (path == null || path.isEmpty()) {
            return RootCommandResult.failure("root file path is required");
        }

        return RootCommandResult.fromCommandResult(
                runRootCommand(statCommand(shellQuote(path)), DEFAULT_TIMEOUT_MS)
        );
    }

    public static RootCommandResult listFiles(String path) {
        if (path == null || path.isEmpty()) {
            return RootCommandResult.failure("root file path is required");
        }

        String quotedPath = shellQuote(path);
        String command = "for item in " + quotedPath + "/* " + quotedPath + "/.[!.]* " + quotedPath + "/..?*; do "
                + "[ -e \"$item\" ] || continue; "
                + statCommand("\"$item\"") + "; "
                + "done";
        return RootCommandResult.fromCommandResult(runRootCommand(command, DEFAULT_TIMEOUT_MS));
    }

    public static RootCommandResult removeFile(String path, boolean recursive) {
        if (path == null || path.isEmpty()) {
            return RootCommandResult.failure("root file path is required");
        }
        if (recursive && isUnsafeRecursiveDeletePath(path)) {
            return RootCommandResult.failure("recursive remove path is unsafe");
        }

        String command = recursive
                ? "rm -rf -- " + shellQuote(path)
                : "rm -f -- " + shellQuote(path);
        return RootCommandResult.fromCommandResult(
                runRootCommand(command, DEFAULT_TIMEOUT_MS)
        );
    }

    private static boolean isUnsafeRecursiveDeletePath(String path) {
        String value = path.trim();
        return value.isEmpty() || "/".equals(value) || ".".equals(value) || "..".equals(value);
    }

    public static RootCommandResult makeDirectory(String path, boolean recursive) {
        if (path == null || path.isEmpty()) {
            return RootCommandResult.failure("root file path is required");
        }

        String command = recursive
                ? "mkdir -p -- " + shellQuote(path)
                : "mkdir -- " + shellQuote(path);
        return RootCommandResult.fromCommandResult(runRootCommand(command, DEFAULT_TIMEOUT_MS));
    }

    public static RootCommandResult chmod(String path, String mode) {
        if (path == null || path.isEmpty()) {
            return RootCommandResult.failure("root file path is required");
        }
        if (mode == null || !mode.matches("[0-7]{3,4}")) {
            return RootCommandResult.failure("chmod mode must be 3 or 4 octal digits");
        }

        return RootCommandResult.fromCommandResult(
                runRootCommand("chmod " + mode + " -- " + shellQuote(path), DEFAULT_TIMEOUT_MS)
        );
    }

    public static RootCommandResult chown(String path, String owner) {
        if (path == null || path.isEmpty()) {
            return RootCommandResult.failure("root file path is required");
        }
        if (owner == null || !owner.matches("[A-Za-z0-9_.-]+(:[A-Za-z0-9_.-]+)?")) {
            return RootCommandResult.failure("chown owner must be user or user:group");
        }

        return RootCommandResult.fromCommandResult(
                runRootCommand("chown " + owner + " -- " + shellQuote(path), DEFAULT_TIMEOUT_MS)
        );
    }

    private static String statCommand(String quotedPath) {
        return "stat -c '%F|%s|%a|%U|%G|%u|%g|%Y|%n' -- " + quotedPath;
    }

    public static RootCommandResult pidOf(String processName) {
        if (processName == null || processName.trim().isEmpty()) {
            return RootCommandResult.failure("process name is required");
        }

        String command = "pidof " + shellQuote(processName.trim()) + " 2>/dev/null || true";
        return RootCommandResult.fromCommandResult(runRootCommand(command, DEFAULT_TIMEOUT_MS));
    }

    public static RootCommandResult listProcesses() {
        return RootCommandResult.fromCommandResult(
                runRootCommand(processListCommand(), DEFAULT_TIMEOUT_MS)
        );
    }

    public static RootCommandResult processInfo(String pidOrName) {
        if (pidOrName == null || pidOrName.trim().isEmpty()) {
            return RootCommandResult.failure("process id or name is required");
        }

        String target = pidOrName.trim();
        String command;
        if (target.matches("\\d+")) {
            command = "ps -p " + target + " -o PID,PPID,USER,NAME,ARGS";
        } else {
            command = "pids=$(pidof " + shellQuote(target) + " 2>/dev/null || true); "
                    + "[ -n \"$pids\" ] || exit 1; "
                    + "pid_args=$(printf '%s' \"$pids\" | tr ' ' ','); "
                    + "ps -p \"$pid_args\" -o PID,PPID,USER,NAME,ARGS";
        }
        return RootCommandResult.fromCommandResult(runRootCommand(command, DEFAULT_TIMEOUT_MS));
    }

    public static RootCommandResult processStats(String pidOrName) {
        if (pidOrName == null || pidOrName.trim().isEmpty()) {
            return RootCommandResult.failure("process id or name is required");
        }

        // 资源统计直接读取 /proc/<pid>/status。传进程名时只取第一个 PID，
        // 避免同名多进程时返回多段 status 导致 Lua/HTTP 侧解析不稳定。
        String target = pidOrName.trim();
        String command;
        if (target.matches("\\d+")) {
            command = processStatsCommand(target);
        } else {
            command = "pid=$(pidof " + shellQuote(target) + " 2>/dev/null | awk '{print $1}'); "
                    + "[ -n \"$pid\" ] || exit 1; "
                    + processStatsCommand("$pid");
        }
        return RootCommandResult.fromCommandResult(runRootCommand(command, DEFAULT_TIMEOUT_MS));
    }

    public static RootCommandResult killProcess(String pidOrName, int signal) {
        if (pidOrName == null || pidOrName.trim().isEmpty()) {
            return RootCommandResult.failure("process id or name is required");
        }

        int safeSignal = signal <= 0 ? 15 : signal;
        String target = pidOrName.trim();
        String command;
        if (target.matches("\\d+")) {
            command = "kill -" + safeSignal + " " + target;
        } else {
            command = "pids=$(pidof " + shellQuote(target) + " 2>/dev/null || true); "
                    + "[ -n \"$pids\" ] || exit 1; "
                    + "kill -" + safeSignal + " $pids";
        }
        return RootCommandResult.fromCommandResult(runRootCommand(command, DEFAULT_TIMEOUT_MS));
    }

    private static String processListCommand() {
        // Android toybox `ps` 支持 -o 指定列；明确列顺序后，Java/HTTP 和 Lua 都可以稳定解析。
        return "ps -A -o PID,PPID,USER,NAME,ARGS";
    }

    private static String processStatsCommand(String pidExpression) {
        // pidExpression 可以是普通数字，也可以是 shell 变量 $pid；
        // 这里不加 shellQuote，保留变量展开能力。
        String statusPath = "/proc/" + pidExpression + "/status";
        return "[ -r " + statusPath + " ] || exit 1; cat " + statusPath;
    }

    public static boolean tap(int x, int y) {
        return runInputCommand("tap " + x + " " + y);
    }

    public static boolean swipe(int x1, int y1, int x2, int y2, int durationMs) {
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

    public static boolean paste() {
        return keyEvent(KEYCODE_PASTE);
    }

    public static boolean keyEvent(int keyCode) {
        return runInputCommand("keyevent " + keyCode);
    }

    /**
     * 使用 Android input text 做第一版文本输入。
     *
     * Android input 命令把空格写成 %s；换行和复杂中文在不同系统上表现不稳定，
     * 这里先明确拒绝，后续改为输入法或剪贴板方案时再扩展。
     */
    public static boolean inputText(String text) {
        if (text == null || text.indexOf('\n') >= 0 || text.indexOf('\r') >= 0) {
            return false;
        }
        return runInputCommand("text " + shellQuote(text.replace(" ", "%s")));
    }

    private static boolean runInputCommand(String inputArgs) {
        String command = "input " + inputArgs;
        CommandResult result = runPersistentRootCommand(command, DEFAULT_TIMEOUT_MS);
        return result.exitCode == 0;
    }

    private static RootCommandMode detectRootCommandMode() {
        RootCommandMode mode = RootCommandMode.SU_C;
        CommandResult result = runCommand(mode.buildArgs("id -u"), DEFAULT_TIMEOUT_MS);
        List<RootStatus.ProbeAttempt> attempts = Collections.singletonList(makeProbeAttempt(mode, result));
        if (result.exitCode == 0 && isRootIdentityOutput(result.stdoutText())) {
            cachedRootSuPath = mode.suPath;
            lastRootError = "";
            lastProbeAttempts = attempts;
            return mode;
        }

        cachedRootSuPath = "";
        lastRootError = resolveProbeError(attempts);
        lastProbeAttempts = attempts;
        return RootCommandMode.NONE;
    }

    private static RootStatus.ProbeAttempt makeProbeAttempt(
            RootCommandMode mode,
            CommandResult result) {
        String error = "";
        if (result.timedOut) {
            error = "root probe timeout";
        } else if (result.exitCode < 0) {
            error = result.stderrText().isEmpty() ? "root probe failed" : result.stderrText();
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
            return "root probe did not run";
        }

        for (RootStatus.ProbeAttempt attempt : attempts) {
            String text = (attempt.error + "\n" + attempt.stderr).toLowerCase();
            if (text.contains("permission denied")) {
                return "root permission denied for app process";
            }
        }

        for (RootStatus.ProbeAttempt attempt : attempts) {
            if (!attempt.error.isEmpty() && !attempt.error.contains("No such file or directory")) {
                return attempt.error;
            }
            if (!attempt.stderr.isEmpty() && !attempt.stderr.contains("No such file or directory")) {
                return attempt.stderr;
            }
        }

        RootStatus.ProbeAttempt lastAttempt = attempts.get(attempts.size() - 1);
        if (!lastAttempt.error.isEmpty()) {
            return lastAttempt.error;
        }
        if (!lastAttempt.stderr.isEmpty()) {
            return lastAttempt.stderr;
        }
        if (!lastAttempt.stdout.isEmpty()) {
            return "root probe did not return uid 0: " + lastAttempt.stdout.trim();
        }
        return "root is not available";
    }

    private static boolean isRootIdentityOutput(String text) {
        if (text == null) {
            return false;
        }

        // 部分 su 环境会把 `id -u` 展开成完整 id 输出，例如 `uid=0(root) ...`。
        // 这里把这种格式也视为 root，避免误判 adb root 镜像或特殊 su 实现。
        String value = text.trim();
        return "0".equals(value)
                || value.startsWith("uid=0(")
                || value.startsWith("uid=0 ");
    }

    static CommandResult runRootCommand(String command, long timeoutMs) {
        return runPersistentRootCommandWithOutput(command, timeoutMs);
    }

    static CommandResult runRootBinaryCommand(String command, long timeoutMs) {
        if (!isRootRuntimeReady()) {
            return CommandResult.failure(lastRootError.isEmpty() ? "root is not available" : lastRootError);
        }
        return runCommand(cachedRootCommandMode.buildArgs(command), timeoutMs);
    }

    private static synchronized CommandResult runPersistentRootCommandWithOutput(
            String command,
            long timeoutMs
    ) {
        if (!isRootRuntimeReady()) {
            return CommandResult.failure(lastRootError.isEmpty()
                    ? "Root 运行层未就绪"
                    : lastRootError);
        }

        try {
            CommandResult result = rootShellSession.runTextCommand(command, timeoutMs);
            if (result.timedOut || result.exitCode < 0) {
                closeRootShellSession();
                rootRuntimeAvailable = false;
            }
            return result;
        } catch (IOException exception) {
            closeRootShellSession();
            rootRuntimeAvailable = false;
            return CommandResult.failure(exception.getMessage());
        }
    }

    private static synchronized CommandResult runPersistentRootCommand(String command, long timeoutMs) {
        if (!isRootRuntimeReady()) {
            return CommandResult.failure("Root 运行层未就绪");
        }

        try {
            CommandResult result = rootShellSession.runNoOutputCommand(command, timeoutMs);
            if (result.timedOut || result.exitCode < 0) {
                closeRootShellSession();
                rootRuntimeAvailable = false;
            }
            return result;
        } catch (IOException exception) {
            closeRootShellSession();
            rootRuntimeAvailable = false;
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

    static String shellQuote(String value) {
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
     * 无输出命令只打印退出码；有输出命令会把 stdout/stderr 各自写入临时文件，
     * 再用 base64 包起来传回 Java。这样普通换行、中文和命令输出里的特殊字符
     * 不会破坏协议边界，也不会为每个 API 重新启动 su。
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

        private CommandResult runTextCommand(String command, long timeoutMs) throws IOException {
            if (!isAlive()) {
                return CommandResult.failure("root shell is not alive");
            }

            String marker = MARKER_PREFIX
                    + System.currentTimeMillis()
                    + "_"
                    + (++commandSequence)
                    + "__";
            String stdoutPath = "/data/local/tmp/ael_stdout_" + commandSequence + "_" + System.nanoTime();
            String stderrPath = "/data/local/tmp/ael_stderr_" + commandSequence + "_" + System.nanoTime();
            String script = "{ " + command + "; } >" + shellQuote(stdoutPath)
                    + " 2>" + shellQuote(stderrPath) + "\n"
                    + "rc=$?\n"
                    + "printf '" + TEXT_STDOUT_BEGIN + "\\n'\n"
                    + "base64 " + shellQuote(stdoutPath) + " 2>/dev/null || true\n"
                    + "printf '\\n" + TEXT_STDOUT_END + "\\n'\n"
                    + "printf '" + TEXT_STDERR_BEGIN + "\\n'\n"
                    + "base64 " + shellQuote(stderrPath) + " 2>/dev/null || true\n"
                    + "printf '\\n" + TEXT_STDERR_END + "\\n'\n"
                    + "rm -f " + shellQuote(stdoutPath) + " " + shellQuote(stderrPath) + "\n"
                    + "printf '" + marker + ":%s\\n' \"$rc\"\n";
            stdin.write(script.getBytes(StandardCharsets.UTF_8));
            stdin.flush();

            TextCommandFrames frames = new TextCommandFrames();
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

                if (line == null) {
                    continue;
                }

                if (line.startsWith(marker + ":")) {
                    return new CommandResult(
                            parseMarkerExitCode(line, marker),
                            decodeBase64Frame(frames.stdoutBase64),
                            decodeBase64Frame(frames.stderrBase64)
                    );
                }
                frames.accept(line);
            }

            return CommandResult.timeout();
        }

        private byte[] decodeBase64Frame(StringBuilder builder) {
            if (builder.length() == 0) {
                return new byte[0];
            }

            try {
                return android.util.Base64.decode(builder.toString(), android.util.Base64.DEFAULT);
            } catch (IllegalArgumentException exception) {
                return ("base64 decode failed: " + exception.getMessage())
                        .getBytes(StandardCharsets.UTF_8);
            }
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

        /**
         * root shell 的 stdout 是按行读取的。这里用一个很小的状态机接收 base64
         * 分段，避免命令本身的输出和协议标记混在一起。
         */
        private static final class TextCommandFrames {
            private final StringBuilder stdoutBase64 = new StringBuilder();
            private final StringBuilder stderrBase64 = new StringBuilder();
            private int section;

            private void accept(String line) {
                if (TEXT_STDOUT_BEGIN.equals(line)) {
                    section = 1;
                    return;
                }
                if (TEXT_STDOUT_END.equals(line)) {
                    section = 0;
                    return;
                }
                if (TEXT_STDERR_BEGIN.equals(line)) {
                    section = 2;
                    return;
                }
                if (TEXT_STDERR_END.equals(line)) {
                    section = 0;
                    return;
                }

                if (section == 1) {
                    stdoutBase64.append(line.trim());
                } else if (section == 2) {
                    stderrBase64.append(line.trim());
                }
            }
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
