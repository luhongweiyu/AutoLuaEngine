/**
 * 文件用途：由常驻 RootDaemon 启动、监督和强停一次性 uid=0 脚本 Worker。
 */
package com.xiaoyv.engine;

import android.os.Build;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;

/** RootDaemon 自身不加载 libengine.so 或用户 SO，只监督它创建的子进程。 */
final class RootWorkerSupervisor {
    private static final Object LOCK = new Object();

    private static Process workerProcess;
    private static String workerRunId;

    private RootWorkerSupervisor() {
    }

    static void start(
            String runId,
            String token,
            String classPath,
            String packageName,
            String authority,
            String nativeLibraryDirectory
    ) throws IOException {
        if (!validSimpleValue(runId)
                || !RootDaemonClient.isValidToken(token)
                || !new File(classPath).isFile()
                || !new File(nativeLibraryDirectory).isDirectory()
                || packageName == null
                || authority == null) {
            throw new IOException("Root Worker 启动参数无效");
        }

        synchronized (LOCK) {
            stopLocked(null);
            ProcessBuilder builder = new ProcessBuilder(
                    "/system/bin/app_process",
                    "/system/bin",
                    "--nice-name=xiaoyv-worker-" + shortId(runId),
                    "com.xiaoyv.engine.EngineWorkerMain",
                    "--package", packageName,
                    "--run-id", runId,
                    "--token", token,
                    "--authority", authority,
                    "--native-lib-dir", nativeLibraryDirectory
            );
            builder.environment().put("CLASSPATH", classPath);
            String oldLibraryPath = builder.environment().get("LD_LIBRARY_PATH");
            builder.environment().put(
                    "LD_LIBRARY_PATH",
                    nativeLibraryDirectory
                            + (oldLibraryPath == null || oldLibraryPath.isEmpty()
                            ? ""
                            : ":" + oldLibraryPath)
            );
            Process process = builder.start();
            workerProcess = process;
            workerRunId = runId;
            drain(process.getInputStream(), "RootWorkerStdout");
            drain(process.getErrorStream(), "RootWorkerStderr");
            monitor(process, runId);
        }
    }

    static boolean stop(String runId) {
        synchronized (LOCK) {
            return stopLocked(runId);
        }
    }

    static void shutdown() {
        synchronized (LOCK) {
            stopLocked(null);
        }
    }

    private static boolean stopLocked(String expectedRunId) {
        if (workerProcess == null) {
            return expectedRunId == null || expectedRunId.equals(workerRunId);
        }
        if (expectedRunId != null && !expectedRunId.equals(workerRunId)) {
            return false;
        }
        Process process = workerProcess;
        workerProcess = null;
        workerRunId = null;
        process.destroy();
        if (isAlive(process)) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                process.destroyForcibly();
            }
        }
        return true;
    }

    private static void monitor(Process process, String runId) {
        Thread monitor = new Thread(() -> {
            try {
                process.waitFor();
            } catch (InterruptedException exception) {
                Thread.currentThread().interrupt();
            }
            synchronized (LOCK) {
                if (workerProcess == process && runId.equals(workerRunId)) {
                    workerProcess = null;
                    workerRunId = null;
                }
            }
        }, "RootWorkerMonitor");
        monitor.setDaemon(true);
        monitor.start();
    }

    private static void drain(InputStream stream, String name) {
        Thread thread = new Thread(() -> {
            try (InputStream input = stream) {
                byte[] buffer = new byte[1024];
                while (input.read(buffer) != -1) {
                    // Worker 不以 stdout/stderr 作为控制协议。
                }
            } catch (IOException ignored) {
                // 子进程退出时管道自然关闭。
            }
        }, name);
        thread.setDaemon(true);
        thread.start();
    }

    private static boolean isAlive(Process process) {
        try {
            process.exitValue();
            return false;
        } catch (IllegalThreadStateException exception) {
            return true;
        }
    }

    private static boolean validSimpleValue(String value) {
        if (value == null || value.isEmpty() || value.length() > 128) {
            return false;
        }
        for (int index = 0; index < value.length(); index++) {
            char c = value.charAt(index);
            if (!Character.isLetterOrDigit(c) && c != '-' && c != '_') {
                return false;
            }
        }
        return true;
    }

    private static String shortId(String runId) {
        return runId.length() <= 8 ? runId : runId.substring(0, 8);
    }
}
