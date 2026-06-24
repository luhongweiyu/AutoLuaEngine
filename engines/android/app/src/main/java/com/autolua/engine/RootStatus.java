package com.autolua.engine;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * Root 探测状态。
 *
 * 这个对象用于把 root 探测过程暴露给脚本和 IDE。单纯的 true/false 不足以定位
 * “adb shell 有 root，但普通 App 进程没有 root 授权”这类问题。
 */
public final class RootStatus {
    public final boolean available;
    public final String commandMode;
    public final String suPath;
    public final boolean cached;
    public final long cacheExpireAt;
    public final String error;
    public final List<ProbeAttempt> attempts;

    RootStatus(
            boolean available,
            String commandMode,
            String suPath,
            boolean cached,
            long cacheExpireAt,
            String error,
            List<ProbeAttempt> attempts
    ) {
        this.available = available;
        this.commandMode = commandMode == null ? "NONE" : commandMode;
        this.suPath = suPath == null ? "" : suPath;
        this.cached = cached;
        this.cacheExpireAt = cacheExpireAt;
        this.error = error == null ? "" : error;
        this.attempts = attempts == null
                ? Collections.emptyList()
                : Collections.unmodifiableList(new ArrayList<>(attempts));
    }

    public static final class ProbeAttempt {
        public final String commandMode;
        public final String suPath;
        public final int exitCode;
        public final String stdout;
        public final String stderr;
        public final boolean timedOut;
        public final String error;

        ProbeAttempt(
                String commandMode,
                String suPath,
                int exitCode,
                String stdout,
                String stderr,
                boolean timedOut,
                String error
        ) {
            this.commandMode = commandMode == null ? "" : commandMode;
            this.suPath = suPath == null ? "" : suPath;
            this.exitCode = exitCode;
            this.stdout = stdout == null ? "" : stdout;
            this.stderr = stderr == null ? "" : stderr;
            this.timedOut = timedOut;
            this.error = error == null ? "" : error;
        }
    }
}
