package com.autolua.engine;

/**
 * Root 命令执行结果。
 *
 * `exitCode != 0` 仍然是一次成功完成的命令调用，只是命令本身失败；
 * `error` 用于描述 root 不可用、命令超时、JNI 调用失败这类通道层问题。
 */
public final class RootCommandResult {
    public final boolean success;
    public final int exitCode;
    public final String stdout;
    public final String stderr;
    public final boolean timedOut;
    public final String error;

    private RootCommandResult(
            boolean success,
            int exitCode,
            String stdout,
            String stderr,
            boolean timedOut,
            String error
    ) {
        this.success = success;
        this.exitCode = exitCode;
        this.stdout = stdout == null ? "" : stdout;
        this.stderr = stderr == null ? "" : stderr;
        this.timedOut = timedOut;
        this.error = error == null ? "" : error;
    }

    static RootCommandResult fromCommandResult(RootShellBridge.CommandResult result) {
        String stderr = result.stderrText();
        String error = "";
        if (result.timedOut) {
            error = "root command timeout";
        } else if (result.exitCode < 0) {
            error = stderr.isEmpty() ? "root command failed" : stderr;
        }

        return new RootCommandResult(
                result.exitCode == 0,
                result.exitCode,
                result.stdoutText(),
                stderr,
                result.timedOut,
                error
        );
    }

    static RootCommandResult failure(String error) {
        return new RootCommandResult(false, -1, "", "", false, error);
    }
}
