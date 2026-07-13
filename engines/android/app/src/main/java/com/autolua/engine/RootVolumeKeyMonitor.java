/**
 * 文件用途：在 App 主进程维护 RootDaemon 音量键订阅，并转发为脚本运行、停止命令。
 */
package com.autolua.engine;

import android.content.Context;

import java.io.IOException;
import java.net.Socket;

/**
 * Root 音量键订阅客户端。
 *
 * RootDaemonService 在 Root 初始化成功后启动一个长连接。监听线程只接收轻量文本事件，
 * 不持有脚本 VM；实际运行和停止仍通过 EngineService Intent 进入独立的 :engine 进程。
 */
final class RootVolumeKeyMonitor {
    private final Context appContext;

    private volatile boolean running;
    private volatile Socket socket;
    private Thread worker;
    private long generation;

    RootVolumeKeyMonitor(Context context) {
        appContext = context.getApplicationContext();
    }

    /**
     * 启动订阅。方法可重复调用，已有有效监听线程时不会创建第二条连接。
     */
    synchronized void start() {
        if (worker != null && worker.isAlive()) {
            return;
        }
        running = true;
        long workerGeneration = ++generation;
        worker = new Thread(
                () -> listenLoop(workerGeneration),
                "RootVolumeKeyMonitor"
        );
        worker.start();
    }

    /**
     * 关闭 socket 会立即解除阻塞读取；不等待线程结束，避免阻塞 Android Service 主线程。
     */
    synchronized void stop() {
        running = false;
        generation++;
        closeSocket();
        if (worker != null) {
            worker.interrupt();
            worker = null;
        }
    }

    /**
     * 完成认证并订阅事件。RootDaemon 已由同一 Service 确认就绪，因此这里连接失败时直接
     * 结束，下一次 Root 模式或音量键设置同步会重新建立连接，不执行 su 或其他兜底命令。
     */
    private void listenLoop(long workerGeneration) {
        Socket activeSocket = null;
        try {
            activeSocket = RootDaemonClient.openAuthenticatedSocket(
                    appContext,
                    RootDaemonProtocol.CONNECT_TIMEOUT_MS
            );
            synchronized (this) {
                if (!running || generation != workerGeneration) {
                    return;
                }
                socket = activeSocket;
            }
            RootDaemonProtocol.writeLine(
                    activeSocket.getOutputStream(),
                    RootDaemonProtocol.SUBSCRIBE_VOLUME_KEYS_COMMAND
            );
            if (!RootDaemonProtocol.isOk(
                    RootDaemonProtocol.readLine(activeSocket.getInputStream()),
                    "volumeKeysSubscribed"
            )) {
                return;
            }

            String line;
            while (isCurrentWorker(workerGeneration)
                    && (line = RootDaemonProtocol.readLine(activeSocket.getInputStream())) != null) {
                if (!line.startsWith("EVENT\t")) {
                    continue;
                }
                int keyCode = parseKeyCode(line.substring("EVENT\t".length()));
                if (keyCode > 0) {
                    VolumeKeyController.handleKeyDown(appContext, keyCode);
                }
            }
        } catch (IOException ignored) {
            // RootDaemon 关闭或设置切换会主动断开连接，监听线程随之结束。
        } finally {
            synchronized (this) {
                if (socket == activeSocket) {
                    socket = null;
                }
                if (Thread.currentThread() == worker) {
                    worker = null;
                }
            }
            closeSocket(activeSocket);
        }
    }

    private synchronized boolean isCurrentWorker(long workerGeneration) {
        return running && generation == workerGeneration;
    }

    private int parseKeyCode(String text) {
        try {
            return Integer.parseInt(text);
        } catch (NumberFormatException exception) {
            return -1;
        }
    }

    private void closeSocket() {
        Socket currentSocket = socket;
        socket = null;
        closeSocket(currentSocket);
    }

    private void closeSocket(Socket currentSocket) {
        if (currentSocket == null) {
            return;
        }
        try {
            currentSocket.close();
        } catch (IOException ignored) {
            // socket 已经关闭时无需重复处理。
        }
    }
}
