/**
 * 文件用途：定义 App 与常驻 RootDaemon 之间共享的本地认证协议。
 */
package com.autolua.engine;

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
    static final int PORT = 18381;
    static final int CONNECT_TIMEOUT_MS = 1200;
    static final int AUTH_TIMEOUT_MS = 2500;
    static final String TOKEN_FILE_NAME = "root_daemon.token";
    static final String AUTH_COMMAND = "auth";
    static final String SHUTDOWN_COMMAND = "shutdown";

    private RootDaemonProtocol() {
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
