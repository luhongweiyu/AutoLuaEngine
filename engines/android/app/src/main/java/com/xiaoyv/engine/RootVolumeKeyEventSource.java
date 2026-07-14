/**
 * 文件用途：在常驻 RootDaemon 内直接读取 Linux 输入设备，并向 App 推送物理音量键事件。
 */
package com.xiaoyv.engine;

import android.os.Process;
import android.view.KeyEvent;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;

/**
 * Root 物理音量键事件源。
 *
 * RootDaemon 启动后只创建一组 `/dev/input/event*` 读取线程，后续 App 重新连接不会重复
 * 打开输入设备。Linux input_event 中只接收 EV_KEY 的首次按下值 1，忽略弹起和长按重复值 2。
 * 这里直接读取内核事件，不拉起 getevent 等外部进程。
 */
final class RootVolumeKeyEventSource {
    private static final int EV_KEY = 0x01;
    private static final int KEY_VOLUME_DOWN = 114;
    private static final int KEY_VOLUME_UP = 115;
    private static final int KEY_VALUE_DOWN = 1;
    private static final int KEEP_ALIVE_INTERVAL_MS = 2000;
    private static final int STOP_EVENT = -1;

    private static final Object START_LOCK = new Object();
    private static final List<DeviceReader> DEVICE_READERS = new ArrayList<>();
    private static final CopyOnWriteArrayList<Subscription> SUBSCRIPTIONS =
            new CopyOnWriteArrayList<>();
    private static volatile boolean running;

    private RootVolumeKeyEventSource() {
    }

    /**
     * 保持当前认证 socket 作为事件订阅通道。
     *
     * 每两秒写入一次 PING，用于及时发现 App 关闭连接；真正的按键以 EVENT 加 Android
     * KeyEvent 按键码推送。订阅连接断开不会关闭 RootDaemon，也不会影响普通命令连接。
     */
    static void streamEvents(OutputStream outputStream) throws IOException {
        Subscription subscription = new Subscription();
        synchronized (START_LOCK) {
            if (!ensureStarted()) {
                RootDaemonProtocol.writeLine(
                        outputStream,
                        "ERR\tvolume input device unavailable"
                );
                return;
            }
            SUBSCRIPTIONS.add(subscription);
        }

        try {
            RootDaemonProtocol.writeLine(outputStream, "OK\tvolumeKeysSubscribed");
            while (running) {
                Integer keyCode;
                try {
                    keyCode = subscription.events.poll(
                            KEEP_ALIVE_INTERVAL_MS,
                            TimeUnit.MILLISECONDS
                    );
                } catch (InterruptedException exception) {
                    Thread.currentThread().interrupt();
                    return;
                }

                if (keyCode == null) {
                    RootDaemonProtocol.writeLine(outputStream, "PING");
                } else if (keyCode == STOP_EVENT) {
                    return;
                } else {
                    RootDaemonProtocol.writeLine(outputStream, "EVENT\t" + keyCode);
                }
            }
        } finally {
            synchronized (START_LOCK) {
                SUBSCRIPTIONS.remove(subscription);
                if (SUBSCRIPTIONS.isEmpty()) {
                    stopDeviceReadersLocked();
                }
            }
        }
    }

    /**
     * RootDaemon 关闭时停止全部设备读取线程，并唤醒仍在等待事件的订阅连接。
     */
    static void shutdown() {
        synchronized (START_LOCK) {
            stopDeviceReadersLocked();
            for (Subscription subscription : SUBSCRIPTIONS) {
                subscription.events.offer(STOP_EVENT);
            }
            SUBSCRIPTIONS.clear();
        }
    }

    /**
     * 关闭全部 evdev 文件。调用方必须持有 START_LOCK，保证旧订阅关闭与新订阅建立不会交叉。
     */
    private static void stopDeviceReadersLocked() {
        running = false;
        for (DeviceReader reader : DEVICE_READERS) {
            reader.close();
        }
        DEVICE_READERS.clear();
    }

    /**
     * 首次订阅时打开当前系统全部 evdev 节点。每个节点只打开一次，后续订阅复用事件源。
     */
    private static boolean ensureStarted() {
        synchronized (START_LOCK) {
            if (running) {
                return !DEVICE_READERS.isEmpty();
            }

            File inputDirectory = new File("/dev/input");
            File[] eventFiles = inputDirectory.listFiles(
                    file -> file.getName().matches("event\\d+")
            );
            if (eventFiles == null || eventFiles.length == 0) {
                return false;
            }
            Arrays.sort(eventFiles, (left, right) -> left.getName().compareTo(right.getName()));

            for (File eventFile : eventFiles) {
                try {
                    DEVICE_READERS.add(new DeviceReader(eventFile));
                } catch (IOException ignored) {
                    // 单个节点无法打开时跳过；没有任何可读节点才判定初始化失败。
                }
            }
            if (DEVICE_READERS.isEmpty()) {
                return false;
            }

            running = true;
            for (DeviceReader reader : DEVICE_READERS) {
                reader.start();
            }
            return true;
        }
    }

    /**
     * 把一次物理按键广播给当前订阅者。Android KeyEvent 按键码与 Linux 音量键码分别是
     * 24/25 和 115/114，因此在 RootDaemon 内完成一次明确转换。
     */
    private static void publishLinuxKey(int linuxKeyCode) {
        int androidKeyCode;
        if (linuxKeyCode == KEY_VOLUME_UP) {
            androidKeyCode = KeyEvent.KEYCODE_VOLUME_UP;
        } else if (linuxKeyCode == KEY_VOLUME_DOWN) {
            androidKeyCode = KeyEvent.KEYCODE_VOLUME_DOWN;
        } else {
            return;
        }

        for (Subscription subscription : SUBSCRIPTIONS) {
            subscription.events.offer(androidKeyCode);
        }
    }

    /**
     * 一个已认证 App 连接对应一个独立队列，慢连接不会阻塞输入设备读取线程。
     */
    private static final class Subscription {
        private final LinkedBlockingQueue<Integer> events = new LinkedBlockingQueue<>();
    }

    /**
     * 单个 Linux evdev 节点的持续读取器。
     */
    private static final class DeviceReader {
        private final FileInputStream inputStream;
        private final Thread thread;
        private final int eventSize;
        private final int fieldsOffset;

        private DeviceReader(File eventFile) throws IOException {
            inputStream = new FileInputStream(eventFile);
            // input_event = timeval + type(2) + code(2) + value(4)。64 位 timeval 为 16
            // 字节，32 位为 8 字节；RootDaemon 必须按自身 app_process 位数解析。
            fieldsOffset = Process.is64Bit() ? 16 : 8;
            eventSize = fieldsOffset + 8;
            thread = new Thread(this::readLoop, "RootVolumeKey-" + eventFile.getName());
            thread.setDaemon(true);
        }

        private void start() {
            thread.start();
        }

        /**
         * FileInputStream 可能返回半个 input_event，因此保留尾部字节并在下一次读取后继续解析。
         */
        private void readLoop() {
            byte[] buffer = new byte[eventSize * 32];
            int bufferedBytes = 0;
            try {
                while (running) {
                    int count = inputStream.read(buffer, bufferedBytes, buffer.length - bufferedBytes);
                    if (count < 0) {
                        return;
                    }
                    bufferedBytes += count;

                    int offset = 0;
                    while (bufferedBytes - offset >= eventSize) {
                        ByteBuffer event = ByteBuffer.wrap(buffer).order(ByteOrder.nativeOrder());
                        event.position(offset + fieldsOffset);
                        int type = event.getShort() & 0xffff;
                        int code = event.getShort() & 0xffff;
                        int value = event.getInt();
                        if (type == EV_KEY && value == KEY_VALUE_DOWN) {
                            publishLinuxKey(code);
                        }
                        offset += eventSize;
                    }

                    if (offset > 0) {
                        bufferedBytes -= offset;
                        if (bufferedBytes > 0) {
                            System.arraycopy(buffer, offset, buffer, 0, bufferedBytes);
                        }
                    }
                }
            } catch (IOException ignored) {
                // RootDaemon 关闭时会主动关闭流以解除阻塞；单节点异常不影响其他输入设备。
            } finally {
                close();
            }
        }

        private void close() {
            try {
                inputStream.close();
            } catch (IOException ignored) {
                // 文件已经关闭时无需重复处理。
            }
        }
    }
}
