/**
 * 文件用途：RootDaemon 的特权命令分发器，执行截图、输入和输入法 Root 能力。
 */
package com.xiaoyv.engine;

import android.graphics.Bitmap;
import android.util.DisplayMetrics;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import android.util.Base64;
import java.nio.charset.StandardCharsets;

/**
 * RootDaemon 特权命令分发器。
 *
 * 此类不再作为独立进程入口。RootDaemonMain 负责受限的 loopback socket、客户端认证和
 * 生命周期；认证完成后把命令交给本类。截图使用“文本头 + 原始二进制帧”，避免 Base64
 * 造成整帧额外复制。普通脚本命令不会拉起外部 shell；只有输入法锁定或解锁时才按 Android
 * 系统要求执行一次 ime/settings 命令。
 */
public final class RootHelperMain {
    /**
     * Root 注入器和截图缓冲属于进程内单例。RootDaemon 允许多个客户端连接，但同一时刻
     * 只允许一个命令操作这些状态，避免二进制截图流与输入事件交叉。
     */
    private static final Object COMMAND_LOCK = new Object();
    private static byte[] captureBytes;
    private static ByteBuffer captureBuffer;
    private static final RootInputInjector INPUT_INJECTOR = new RootInputInjector();

    private RootHelperMain() {
    }

    /**
     * 分发一条已经完成认证的 Root 命令。
     *
     * 返回 false 表示当前客户端应关闭，RootDaemon 本身不会因此退出。
     */
    static boolean dispatchCommand(OutputStream outputStream, String[] parts) throws Exception {
        synchronized (COMMAND_LOCK) {
            if (parts == null || parts.length == 0 || parts[0].isEmpty()) {
                writeLine(outputStream, "ERR\tunknown command");
                return true;
            }

            if ("ping".equals(parts[0])) {
                writeLine(outputStream, "OK\tpong");
                return true;
            }

            if ("exit".equals(parts[0])) {
                writeLine(outputStream, "OK\tbye");
                return false;
            }

            if ("capture".equals(parts[0])) {
                handleCapture(outputStream, parts);
                return true;
            }

            if ("touchDown".equals(parts[0])) {
                writeBoolean(outputStream, handleTouchDown(parts));
                return true;
            }

            if ("touchMove".equals(parts[0])) {
                writeBoolean(outputStream, handleTouchMove(parts));
                return true;
            }

            if ("touchUp".equals(parts[0])) {
                writeBoolean(outputStream, handleTouchUp(parts));
                return true;
            }

            if ("keyDown".equals(parts[0])) {
                writeBoolean(outputStream, handleKeyDown(parts));
                return true;
            }

            if ("keyUp".equals(parts[0])) {
                writeBoolean(outputStream, handleKeyUp(parts));
                return true;
            }

            if ("keyPress".equals(parts[0])) {
                writeBoolean(outputStream, handleKeyPress(parts));
                return true;
            }

            if ("inputText".equals(parts[0])) {
                writeBoolean(outputStream, handleInputText(parts));
                return true;
            }

            if ("imeLock".equals(parts[0])) {
                handleImeLock(outputStream);
                return true;
            }

            if ("imeUnlock".equals(parts[0])) {
                writeBoolean(outputStream, handleImeUnlock(parts));
                return true;
            }

            writeLine(outputStream, "ERR\tunknown command");
            return true;
        }
    }

    private static void handleCapture(OutputStream outputStream, String[] parts) throws Exception {
        int width = parts.length >= 2 ? parseInt(parts[1], 0) : 0;
        int height = parts.length >= 3 ? parseInt(parts[2], 0) : 0;
        if (width <= 0 || height <= 0) {
            DisplayMetrics metrics = RootHelperDisplayMetrics.read();
            width = metrics.widthPixels;
            height = metrics.heightPixels;
        }

        Bitmap bitmap = SurfaceScreenCaptureBridge.captureBitmapForRootHelper(width, height);
        if (bitmap == null) {
            writeLine(outputStream, "ERR\tRoot 截图服务返回了空位图");
            return;
        }
        if (!Bitmap.Config.ARGB_8888.equals(bitmap.getConfig())) {
            Bitmap copy = bitmap.copy(Bitmap.Config.ARGB_8888, false);
            bitmap.recycle();
            if (copy == null) {
                writeLine(outputStream, "ERR\tRoot 截图服务复制硬件位图失败");
                return;
            }
            bitmap = copy;
        }

        int bitmapWidth = bitmap.getWidth();
        int bitmapHeight = bitmap.getHeight();
        int pixelBytes = bitmapWidth * bitmapHeight * 4;
        ByteBuffer pixels = ensureCaptureBuffer(pixelBytes);
        bitmap.copyPixelsToBuffer(pixels);
        bitmap.recycle();

        writeLine(outputStream, "OK\t"
                + bitmapWidth
                + "\t"
                + bitmapHeight
                + "\t"
                + pixelBytes);
        outputStream.write(captureBytes, 0, pixelBytes);
        outputStream.flush();
    }

    private static boolean handleTouchDown(String[] parts) {
        if (parts.length < 4) {
            return false;
        }
        return INPUT_INJECTOR.touchDown(
                parseInt(parts[1], -1),
                parseInt(parts[2], -1),
                parseInt(parts[3], -1)
        );
    }

    private static boolean handleTouchMove(String[] parts) {
        if (parts.length < 4) {
            return false;
        }
        return INPUT_INJECTOR.touchMove(
                parseInt(parts[1], -1),
                parseInt(parts[2], -1),
                parseInt(parts[3], -1)
        );
    }

    private static boolean handleTouchUp(String[] parts) {
        if (parts.length < 2) {
            return false;
        }
        return INPUT_INJECTOR.touchUp(parseInt(parts[1], -1));
    }

    private static boolean handleKeyDown(String[] parts) {
        if (parts.length < 2) {
            return false;
        }
        return INPUT_INJECTOR.keyDown(parseInt(parts[1], 0));
    }

    private static boolean handleKeyUp(String[] parts) {
        if (parts.length < 2) {
            return false;
        }
        return INPUT_INJECTOR.keyUp(parseInt(parts[1], 0));
    }

    private static boolean handleKeyPress(String[] parts) {
        if (parts.length < 2) {
            return false;
        }
        return INPUT_INJECTOR.keyPress(parseInt(parts[1], 0));
    }

    private static boolean handleInputText(String[] parts) {
        if (parts.length < 2) {
            return false;
        }
        try {
            byte[] bytes = Base64.decode(parts[1], Base64.NO_WRAP);
            return INPUT_INJECTOR.inputText(new String(bytes, StandardCharsets.UTF_8));
        } catch (IllegalArgumentException exception) {
            return false;
        }
    }

    /**
     * 读取当前默认输入法，启用并切换到 小鱼精灵 输入法。
     *
     * 旧输入法名称以 Base64 写入协议，避免未来带有特殊字符时破坏文本头格式。这里不做
     * 按键输入或无障碍回退；切换失败直接把失败结果返回给上层。
     */
    private static void handleImeLock(OutputStream outputStream) throws Exception {
        String previousInputMethod = readDefaultInputMethod();
        if (previousInputMethod == null || previousInputMethod.isEmpty()) {
            writeLine(outputStream, "ERR\tunable to read default input method");
            return;
        }

        if (!setInputMethodEnabled(EngineImeBridge.inputMethodComponent(), true)
                || !setDefaultInputMethod(EngineImeBridge.inputMethodComponent())) {
            writeLine(outputStream, "ERR\tunable to select engine input method");
            return;
        }

        String encodedPrevious = Base64.encodeToString(
                previousInputMethod.getBytes(StandardCharsets.UTF_8),
                Base64.NO_WRAP
        );
        writeLine(outputStream, "OK\t" + encodedPrevious);
    }

    /**
     * 恢复指定默认输入法并禁用 小鱼精灵 输入法。
     *
     * 原输入法由 EngineImeBridge 在 lock 成功后持久保存，故 helper 重启后仍然可以完成
     * unlock。参数全部使用 Base64 传输，不经 shell 拼接。
     */
    private static boolean handleImeUnlock(String[] parts) {
        if (parts.length < 3) {
            return false;
        }

        try {
            String previousInputMethod = new String(
                    Base64.decode(parts[1], Base64.NO_WRAP),
                    StandardCharsets.UTF_8
            );
            String engineInputMethod = new String(
                    Base64.decode(parts[2], Base64.NO_WRAP),
                    StandardCharsets.UTF_8
            );
            if (!isValidInputMethodId(previousInputMethod)
                    || !EngineImeBridge.inputMethodComponent().equals(engineInputMethod)) {
                return false;
            }

            return setDefaultInputMethod(previousInputMethod)
                    && setInputMethodEnabled(engineInputMethod, false);
        } catch (IllegalArgumentException exception) {
            return false;
        }
    }

    /**
     * 读取系统安全设置中的当前默认输入法组件名。
     */
    private static String readDefaultInputMethod() {
        CommandResult result = executeCommand(
                "/system/bin/settings",
                "get",
                "secure",
                "default_input_method"
        );
        if (!result.succeeded()) {
            return null;
        }

        String value = result.stdout.trim();
        return value.isEmpty() || "null".equals(value) ? null : value;
    }

    /**
     * 通过系统 ime 命令启用或禁用一个输入法服务。
     */
    private static boolean setInputMethodEnabled(String inputMethodId, boolean enabled) {
        if (!isValidInputMethodId(inputMethodId)) {
            return false;
        }
        CommandResult result = executeCommand(
                "/system/bin/cmd",
                "input_method",
                "ime",
                enabled ? "enable" : "disable",
                inputMethodId
        );
        return result.succeeded();
    }

    /**
     * 通过系统 ime 命令切换默认输入法。
     */
    private static boolean setDefaultInputMethod(String inputMethodId) {
        if (!isValidInputMethodId(inputMethodId)) {
            return false;
        }
        CommandResult result = executeCommand(
                "/system/bin/cmd",
                "input_method",
                "ime",
                "set",
                inputMethodId
        );
        return result.succeeded();
    }

    /**
     * 输入法 ID 只能是单行组件名，拒绝协议或命令分隔符。
     */
    private static boolean isValidInputMethodId(String inputMethodId) {
        return inputMethodId != null
                && !inputMethodId.isEmpty()
                && inputMethodId.indexOf('\n') < 0
                && inputMethodId.indexOf('\r') < 0
                && inputMethodId.indexOf('\t') < 0;
    }

    /**
     * 执行一次固定 Android 系统命令并收集小型标准输出。
     *
     * 仅 imeLock/imeUnlock 调用该方法；高频 setText 不会经过这里，也不会创建外部进程。
     */
    private static CommandResult executeCommand(String... command) {
        try {
            Process process = new ProcessBuilder(command)
                    .redirectErrorStream(true)
                    .start();
            byte[] stdout = readAll(process.getInputStream());
            int exitCode = process.waitFor();
            return new CommandResult(
                    exitCode,
                    new String(stdout, StandardCharsets.UTF_8)
            );
        } catch (Exception exception) {
            return new CommandResult(-1, "");
        }
    }

    /**
     * 读取 ime/settings 的短输出。两条命令输出很小，不会形成管道阻塞。
     */
    private static byte[] readAll(InputStream inputStream) throws Exception {
        try (InputStream source = inputStream; ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[256];
            int count;
            while ((count = source.read(buffer)) != -1) {
                output.write(buffer, 0, count);
            }
            return output.toByteArray();
        }
    }

    /**
     * 固定系统命令的执行结果。
     */
    private static final class CommandResult {
        private final int exitCode;
        private final String stdout;

        private CommandResult(int exitCode, String stdout) {
            this.exitCode = exitCode;
            this.stdout = stdout == null ? "" : stdout;
        }

        private boolean succeeded() {
            return exitCode == 0;
        }
    }

    /**
     * 复用 root helper 进程内的整帧缓冲。
     *
     * Bitmap.copyPixelsToBuffer 每次都需要一个可写 Buffer。这里让 ByteBuffer 包在同一块
     * byte[] 上，屏幕尺寸不变时不再反复分配整帧数组。
     */
    private static ByteBuffer ensureCaptureBuffer(int pixelBytes) {
        if (captureBytes == null || captureBytes.length < pixelBytes) {
            captureBytes = new byte[pixelBytes];
            captureBuffer = ByteBuffer.wrap(captureBytes).order(ByteOrder.nativeOrder());
        }

        captureBuffer.clear();
        captureBuffer.limit(pixelBytes);
        return captureBuffer;
    }

    private static void writeLine(OutputStream outputStream, String text) throws Exception {
        outputStream.write((text + "\n").getBytes(StandardCharsets.UTF_8));
        outputStream.flush();
    }

    private static void writeBoolean(OutputStream outputStream, boolean value) throws Exception {
        writeLine(outputStream, value ? "OK\ttrue" : "OK\tfalse");
    }

    private static int parseInt(String value, int defaultValue) {
        try {
            return Integer.parseInt(value);
        } catch (NumberFormatException exception) {
            return defaultValue;
        }
    }

}
