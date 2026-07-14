/**
 * 文件用途：定义 App 与常驻 RootDaemon 之间共享的本地认证协议。
 */
package com.xiaoyv.engine;

import android.content.Context;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;

/**
 * RootDaemon 本地协议常量和无缓冲文本帧工具。
 *
 * 截图命令会在文本头之后紧跟原始 RGBA 字节，因此这里按字节读取行且不使用会预读数据的
 * BufferedReader，避免把截图像素提前吞进字符缓冲区。
 */
final class RootDaemonProtocol {
    /*
     * RootDaemon 不能再使用所有安装包共用的固定端口。旧包、测试包或不同品牌包若同时存在，
     * 固定端口会让新包连到旧包的 daemon，认证失败后误以为自身 daemon 已退出，并重复执行 su。
     *
     * Android 为每个应用分配不同 Linux UID，因此把 UID 映射到本机高位端口区间即可让同一
     * 设备上的不同安装包稳定隔离。RootDaemon 只接受该区间端口，实际端口随启动参数传入。
     */
    private static final int PORT_BASE = 30000;
    private static final int PORT_SLOT_COUNT = 10000;

    static final int CONNECT_TIMEOUT_MS = 1200;
    static final int AUTH_TIMEOUT_MS = 2500;
    static final String TOKEN_FILE_NAME = "root_daemon.token";
    static final String AUTH_COMMAND = "auth";
    static final String SHUTDOWN_COMMAND = "shutdown";
    static final String OWNER_PID_COMMAND = "ownerPid";
    static final String SUBSCRIBE_VOLUME_KEYS_COMMAND = "subscribeVolumeKeys";

    private RootDaemonProtocol() {
    }

    /**
     * 取得当前安装包专属的回环端口。
     *
     * 同一包的主进程和 :engine 进程共享 UID，因而会得到相同端口；另一个安装包即便保留了
     * 老版本 RootDaemon，也会落在不同端口，不会相互干扰。
     */
    static int port(Context context) {
        int uid = context == null ? 0 : context.getApplicationInfo().uid;
        return PORT_BASE + Math.floorMod(uid, PORT_SLOT_COUNT);
    }

    /**
     * Root 进程只接受本应用约定的高位端口，避免被错误参数启动到调试或系统服务端口。
     */
    static boolean isDaemonPort(int port) {
        return port >= PORT_BASE && port < PORT_BASE + PORT_SLOT_COUNT;
    }

    static void writeLine(OutputStream outputStream, String text) throws IOException {
        outputStream.write((text + "\n").getBytes(StandardCharsets.UTF_8));
        outputStream.flush();
    }

    static String readLine(InputStream inputStream) throws IOException {
        ByteArrayOutputStream outputStream = new ByteArrayOutputStream(128);
        int value;
        while ((value = inputStream.read()) != -1) {
            if (value == '\n') {
                return outputStream.toString(StandardCharsets.UTF_8.name());
            }
            if (value != '\r') {
                outputStream.write(value);
            }
        }
        return outputStream.size() == 0
                ? null
                : outputStream.toString(StandardCharsets.UTF_8.name());
    }

    static boolean isOk(String line, String expectedMessage) {
        return ("OK\t" + expectedMessage).equals(line);
    }
}
