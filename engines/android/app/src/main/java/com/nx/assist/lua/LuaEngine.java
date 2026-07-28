/**
 * 文件用途：提供与懒人精灵同包名、同类名的 LuaEngine Java 兼容入口。
 */
package com.nx.assist.lua;

import android.content.Context;
import android.graphics.Bitmap;
import android.util.Log;

import com.xiaoyv.engine.AndroidHostBridge;
import com.xiaoyv.engine.NativeEngine;

import org.opencv.android.OpenCVLoader;
import org.opencv.android.Utils;
import org.opencv.core.Mat;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URLEncoder;
import java.net.URL;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.util.Collections;
import java.util.Date;
import java.util.Map;
import java.util.Properties;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

import javax.activation.DataHandler;
import javax.activation.FileDataSource;
import javax.mail.Authenticator;
import javax.mail.Message;
import javax.mail.Multipart;
import javax.mail.PasswordAuthentication;
import javax.mail.Session;
import javax.mail.Transport;
import javax.mail.internet.InternetAddress;
import javax.mail.internet.MimeBodyPart;
import javax.mail.internet.MimeMessage;
import javax.mail.internet.MimeMultipart;

/**
 * 懒人精灵 LuaEngine 兼容类。
 *
 * 脚本通过 `import('com.nx.assist.lua.LuaEngine')` 得到该 Class，并由通用 Java
 * 互操作层调用这里的静态方法。方法名称、参数顺序和失败返回 null 的行为与文档
 * 保持一致。
 */
public final class LuaEngine {
    private static final String TAG = "LuaEngineCompat";
    private static final int DEFAULT_TIMEOUT_SECONDS = 30;
    private static final int COPY_BUFFER_BYTES = 16 * 1024;
    private static final ExecutorService MAIL_EXECUTOR = Executors.newFixedThreadPool(2);
    private static final Object OPEN_CV_LOCK = new Object();
    private static final Object EXIT_CALLBACK_LOCK = new Object();
    private static volatile boolean openCvInitialized;
    private static IOnExitCallback exitCallback;

    private LuaEngine() {
    }

    /**
     * 返回引擎进程的 Android Application Context。
     */
    public static Context getContext() {
        return AndroidHostBridge.applicationContext();
    }

    /** 注册当前脚本结束回调；同一脚本重复注册时以后一次为准。 */
    public static void registerExitCallback(IOnExitCallback callback) {
        if (callback == null) {
            throw new IllegalArgumentException("结束回调不能为空");
        }
        synchronized (EXIT_CALLBACK_LOCK) {
            exitCallback = callback;
        }
    }

    /**
     * 由引擎在 LuaRuntime 销毁前调用。回调取出后立即清空，避免代理对象跨脚本生命周期泄漏。
     */
    public static boolean dispatchExitCallback(boolean error, int exitCode) {
        IOnExitCallback callback;
        synchronized (EXIT_CALLBACK_LOCK) {
            callback = exitCallback;
            exitCallback = null;
        }
        if (callback == null) {
            return false;
        }
        callback.onExit(error, exitCode);
        return true;
    }

    /**
     * 使用默认 30 秒超时执行 Java 层 HTTP GET。
     */
    public static String httpGet(String url, Map<?, ?> headers) {
        return httpGet(url, headers, DEFAULT_TIMEOUT_SECONDS);
    }

    /**
     * 执行 Java 层 HTTP GET；timeout 单位为秒。
     */
    public static String httpGet(String url, Map<?, ?> headers, long timeout) {
        HttpURLConnection connection = null;
        try {
            connection = openConnection(url, headers, timeout);
            connection.setRequestMethod("GET");
            connection.connect();
            return readResponseText(connection);
        } catch (Exception exception) {
            Log.e(TAG, "HTTP GET 请求失败：" + exception.getMessage(), exception);
            return null;
        } finally {
            if (connection != null) {
                connection.disconnect();
            }
        }
    }

    /**
     * 使用默认 30 秒超时执行表单格式 Java 层 HTTP POST。
     */
    public static String httpPost(
            String url,
            Map<?, ?> params,
            Map<?, ?> headers
    ) {
        return httpPost(url, params, headers, DEFAULT_TIMEOUT_SECONDS);
    }

    /**
     * 执行表单格式 Java 层 HTTP POST；timeout 单位为秒。
     */
    public static String httpPost(
            String url,
            Map<?, ?> params,
            Map<?, ?> headers,
            long timeout
    ) {
        String body = encodeForm(params);
        return executePost(
                url,
                body.getBytes(StandardCharsets.UTF_8),
                "application/x-www-form-urlencoded; charset=utf-8",
                headers,
                timeout
        );
    }

    /**
     * 发送任意文本数据，第三个参数是 Content-Type，timeout 单位为秒。
     */
    public static String httpPostData(
            String url,
            String data,
            String contentType,
            long timeout
    ) {
        return executePost(
                url,
                String.valueOf(data).getBytes(StandardCharsets.UTF_8),
                contentType,
                Collections.emptyMap(),
                timeout
        );
    }

    /**
     * 加载 assets 中的 APK 插件名称或文件系统绝对路径。
     *
     * assets 查找顺序为原名称、plugins/名称。资源插件会先复制到 code cache，避免
     * DexClassLoader 读取压缩 assets。解析或加载失败返回 null。
     */
    public static ApkLoader loadApk(String nameOrPath) {
        try {
            File pluginFile = resolvePluginFile(nameOrPath);
            return pluginFile == null ? null : new ApkLoader(pluginFile.getAbsolutePath());
        } catch (Exception exception) {
            Log.e(TAG, "APK 插件加载失败：" + exception.getMessage(), exception);
            return null;
        }
    }

    /**
     * 返回当前引擎截图的 ARGB_8888 Bitmap。0,0,0,0 表示完整画面，区域坐标右、下边界不包含。
     */
    public static Bitmap snapShot(int left, int top, int right, int bottom) {
        byte[] frame = NativeEngine.getScreenFrame();
        if (frame == null || frame.length < 12
                || frame[0] != 'X' || frame[1] != 'Y' || frame[2] != 'V' || frame[3] != 'F') {
            return null;
        }

        int frameWidth = readLittleEndianInt(frame, 4);
        int frameHeight = readLittleEndianInt(frame, 8);
        long expectedBytes = 12L + (long) frameWidth * frameHeight * 4L;
        if (frameWidth <= 0 || frameHeight <= 0 || expectedBytes != frame.length) {
            return null;
        }

        if (left == 0 && top == 0 && right == 0 && bottom == 0) {
            right = frameWidth;
            bottom = frameHeight;
        }
        if (left < 0 || top < 0 || right <= left || bottom <= top
                || right > frameWidth || bottom > frameHeight) {
            return null;
        }

        int outputWidth = right - left;
        int outputHeight = bottom - top;
        int[] colors = new int[outputWidth * outputHeight];
        for (int y = 0; y < outputHeight; y++) {
            int sourceOffset = 12 + ((top + y) * frameWidth + left) * 4;
            int outputOffset = y * outputWidth;
            for (int x = 0; x < outputWidth; x++) {
                int red = frame[sourceOffset] & 0xff;
                int green = frame[sourceOffset + 1] & 0xff;
                int blue = frame[sourceOffset + 2] & 0xff;
                int alpha = frame[sourceOffset + 3] & 0xff;
                colors[outputOffset + x] =
                        (alpha << 24) | (red << 16) | (green << 8) | blue;
                sourceOffset += 4;
            }
        }
        return Bitmap.createBitmap(colors, outputWidth, outputHeight, Bitmap.Config.ARGB_8888);
    }

    /**
     * 回收由 snapShot 返回的 Bitmap；重复或空值调用安全。
     */
    public static void releaseBmp(Bitmap bitmap) {
        if (bitmap != null && !bitmap.isRecycled()) {
            bitmap.recycle();
        }
    }

    /**
     * 把 Bitmap 固定为后续找色、找图使用的截图缓存；传 null 恢复物理屏幕。
     */
    public static boolean setSnapCacheBitmap(Bitmap bitmap) {
        if (bitmap != null && bitmap.isRecycled()) {
            return false;
        }
        return NativeEngine.setScreenBitmap(bitmap);
    }

    /**
     * 为 cv.snapShot 返回真实 OpenCV Mat；转换完成后立即回收中间 Bitmap。
     */
    public static Mat snapShotMat(int left, int top, int right, int bottom) {
        if (!ensureOpenCvInitialized()) {
            return null;
        }
        Bitmap bitmap = snapShot(left, top, right, bottom);
        if (bitmap == null) {
            return null;
        }
        Mat result = new Mat();
        try {
            Utils.bitmapToMat(bitmap, result);
            return result;
        } catch (RuntimeException exception) {
            result.release();
            Log.e(TAG, "截图转换 OpenCV Mat 失败：" + exception.getMessage(), exception);
            return null;
        } finally {
            releaseBmp(bitmap);
        }
    }

    public static void sendMail(
            String account,
            String password,
            String recipient,
            String server,
            boolean authentication,
            String subject,
            String content
    ) {
        sendMail(account, password, recipient, server, authentication, subject, content, null);
    }

    public static void sendMail(
            String account,
            String password,
            String recipient,
            String server,
            boolean authentication,
            String subject,
            String content,
            IOnMailResult callback
    ) {
        sendMailAsync(
                account,
                password,
                recipient,
                server,
                authentication,
                subject,
                content,
                new String[0],
                callback
        );
    }

    public static void sendMailWithFile(
            String account,
            String password,
            String recipient,
            String server,
            boolean authentication,
            String subject,
            String content,
            String attachment
    ) {
        sendMailWithFile(
                account,
                password,
                recipient,
                server,
                authentication,
                subject,
                content,
                attachment,
                null
        );
    }

    public static void sendMailWithFile(
            String account,
            String password,
            String recipient,
            String server,
            boolean authentication,
            String subject,
            String content,
            String attachment,
            IOnMailResult callback
    ) {
        sendMailAsync(
                account,
                password,
                recipient,
                server,
                authentication,
                subject,
                content,
                new String[]{attachment},
                callback
        );
    }

    public static void sendMailWithMultiFile(
            String account,
            String password,
            String recipient,
            String server,
            boolean authentication,
            String subject,
            String content,
            String[] attachments
    ) {
        sendMailWithMultiFile(
                account,
                password,
                recipient,
                server,
                authentication,
                subject,
                content,
                attachments,
                null
        );
    }

    public static void sendMailWithMultiFile(
            String account,
            String password,
            String recipient,
            String server,
            boolean authentication,
            String subject,
            String content,
            String[] attachments,
            IOnMailResult callback
    ) {
        sendMailAsync(
                account,
                password,
                recipient,
                server,
                authentication,
                subject,
                content,
                attachments == null ? new String[0] : attachments.clone(),
                callback
        );
    }

    /**
     * 发送 POST 字节并返回完整响应文本；失败返回 null。
     */
    private static String executePost(
            String url,
            byte[] body,
            String contentType,
            Map<?, ?> headers,
            long timeout
    ) {
        HttpURLConnection connection = null;
        try {
            connection = openConnection(url, headers, timeout);
            connection.setRequestMethod("POST");
            connection.setDoOutput(true);
            connection.setRequestProperty(
                    "Content-Type",
                    contentType == null || contentType.isEmpty()
                            ? "application/octet-stream"
                            : contentType
            );
            connection.setFixedLengthStreamingMode(body.length);
            try (OutputStream outputStream = connection.getOutputStream()) {
                outputStream.write(body);
            }
            return readResponseText(connection);
        } catch (Exception exception) {
            Log.e(TAG, "HTTP POST 请求失败：" + exception.getMessage(), exception);
            return null;
        } finally {
            if (connection != null) {
                connection.disconnect();
            }
        }
    }

    /**
     * 创建带超时和请求头的 HttpURLConnection。
     */
    private static HttpURLConnection openConnection(
            String url,
            Map<?, ?> headers,
            long timeoutSeconds
    ) throws IOException {
        HttpURLConnection connection = (HttpURLConnection) new URL(url).openConnection();
        int timeoutMs = timeoutMilliseconds(timeoutSeconds);
        connection.setConnectTimeout(timeoutMs);
        connection.setReadTimeout(timeoutMs);
        connection.setUseCaches(false);
        connection.setInstanceFollowRedirects(true);
        applyHeaders(connection, headers);
        return connection;
    }

    /**
     * 把 Lua table 转换出的 Map 写入 HTTP 请求头。
     */
    private static void applyHeaders(HttpURLConnection connection, Map<?, ?> headers) {
        if (headers == null) {
            return;
        }
        for (Map.Entry<?, ?> entry : headers.entrySet()) {
            if (entry.getKey() != null && entry.getValue() != null) {
                connection.setRequestProperty(
                        String.valueOf(entry.getKey()),
                        String.valueOf(entry.getValue())
                );
            }
        }
    }

    /**
     * 将 POST 参数编码为 application/x-www-form-urlencoded。
     */
    private static String encodeForm(Map<?, ?> params) {
        if (params == null || params.isEmpty()) {
            return "";
        }

        StringBuilder builder = new StringBuilder();
        for (Map.Entry<?, ?> entry : params.entrySet()) {
            if (builder.length() > 0) {
                builder.append('&');
            }
            builder.append(urlEncode(String.valueOf(entry.getKey())));
            builder.append('=');
            builder.append(urlEncode(String.valueOf(entry.getValue())));
        }
        return builder.toString();
    }

    /**
     * 执行 UTF-8 URL 编码；UTF-8 在 Android 平台始终存在。
     */
    private static String urlEncode(String value) {
        try {
            return URLEncoder.encode(value, StandardCharsets.UTF_8.name());
        } catch (Exception exception) {
            throw new IllegalStateException("UTF-8 编码不可用", exception);
        }
    }

    /**
     * 读取成功或错误响应流，并根据 Content-Type charset 解码。
     */
    private static String readResponseText(HttpURLConnection connection) throws IOException {
        int responseCode = connection.getResponseCode();
        InputStream stream = responseCode >= 400
                ? connection.getErrorStream()
                : connection.getInputStream();
        if (stream == null) {
            return "";
        }

        try (InputStream inputStream = stream;
             ByteArrayOutputStream outputStream = new ByteArrayOutputStream()) {
            copy(inputStream, outputStream);
            return new String(outputStream.toByteArray(), responseCharset(connection));
        }
    }

    /**
     * 从响应 Content-Type 中提取 charset，缺省使用 UTF-8。
     */
    private static Charset responseCharset(HttpURLConnection connection) {
        String contentType = connection.getContentType();
        if (contentType != null) {
            String[] parts = contentType.split(";");
            for (String part : parts) {
                String trimmed = part.trim();
                if (trimmed.regionMatches(true, 0, "charset=", 0, 8)) {
                    try {
                        return Charset.forName(trimmed.substring(8).trim());
                    } catch (Exception ignored) {
                        return StandardCharsets.UTF_8;
                    }
                }
            }
        }
        return StandardCharsets.UTF_8;
    }

    /**
     * 将秒超时安全转换为 HttpURLConnection 使用的 int 毫秒。
     */
    private static int timeoutMilliseconds(long timeoutSeconds) {
        long normalized = timeoutSeconds <= 0 ? DEFAULT_TIMEOUT_SECONDS : timeoutSeconds;
        if (normalized >= Integer.MAX_VALUE / 1000L) {
            return Integer.MAX_VALUE;
        }
        return (int) (normalized * 1000L);
    }

    /**
     * 解析绝对插件路径或把 assets 插件复制到 code cache。
     */
    private static File resolvePluginFile(String nameOrPath) throws IOException {
        if (nameOrPath == null || nameOrPath.trim().isEmpty()) {
            return null;
        }

        File directFile = new File(nameOrPath);
        if (directFile.isAbsolute()) {
            return directFile.isFile() ? directFile : null;
        }

        Context context = AndroidHostBridge.applicationContext();
        if (context == null) {
            return null;
        }

        String normalizedName = nameOrPath.trim();
        InputStream assetStream = openPluginAsset(context, normalizedName);
        if (assetStream == null) {
            return null;
        }

        File pluginDirectory = new File(context.getCodeCacheDir(), "lua_apk_plugins/source");
        if (!pluginDirectory.exists() && !pluginDirectory.mkdirs()) {
            assetStream.close();
            return null;
        }

        File target = new File(pluginDirectory, new File(normalizedName).getName());
        try (InputStream inputStream = assetStream;
             FileOutputStream outputStream = new FileOutputStream(target, false)) {
            copy(inputStream, outputStream);
        }
        return target;
    }

    private static void sendMailAsync(
            String account,
            String password,
            String recipient,
            String server,
            boolean authentication,
            String subject,
            String content,
            String[] attachments,
            IOnMailResult callback
    ) {
        MAIL_EXECUTOR.execute(() -> {
            try {
                MimeMessage message = buildMail(
                        account,
                        password,
                        recipient,
                        server,
                        authentication,
                        subject,
                        content,
                        attachments
                );
                Transport.send(message);
                if (callback != null) {
                    callback.onSuccess();
                }
            } catch (Exception exception) {
                String message = exception.getMessage() == null
                        ? exception.toString()
                        : exception.getMessage();
                Log.e(TAG, "邮件发送失败：" + message, exception);
                if (callback != null) {
                    callback.onFailed(message);
                }
            }
        });
    }

    private static MimeMessage buildMail(
            String account,
            String password,
            String recipient,
            String server,
            boolean authentication,
            String subject,
            String content,
            String[] attachments
    ) throws Exception {
        if (account == null || account.trim().isEmpty()
                || recipient == null || recipient.trim().isEmpty()
                || server == null || server.trim().isEmpty()) {
            throw new IllegalArgumentException("账号、收件人和邮箱服务器不能为空");
        }

        Properties properties = new Properties();
        properties.put("mail.smtp.host", server.trim());
        properties.put("mail.smtp.auth", String.valueOf(authentication));
        properties.put("mail.smtp.connectiontimeout", "30000");
        properties.put("mail.smtp.timeout", "30000");
        properties.put("mail.smtp.writetimeout", "30000");

        Authenticator authenticator = authentication
                ? new Authenticator() {
                    @Override
                    protected PasswordAuthentication getPasswordAuthentication() {
                        return new PasswordAuthentication(
                                account,
                                password == null ? "" : password
                        );
                    }
                }
                : null;
        Session session = Session.getInstance(properties, authenticator);
        MimeMessage message = new MimeMessage(session);
        message.setFrom(new InternetAddress(account));
        message.setRecipients(
                Message.RecipientType.TO,
                InternetAddress.parse(recipient, false)
        );
        message.setSubject(subject == null ? "" : subject, StandardCharsets.UTF_8.name());
        message.setSentDate(new Date());

        Multipart multipart = new MimeMultipart();
        MimeBodyPart body = new MimeBodyPart();
        body.setContent(
                content == null ? "" : content,
                "text/html; charset=" + StandardCharsets.UTF_8.name()
        );
        multipart.addBodyPart(body);

        for (String attachment : attachments) {
            if (attachment == null || attachment.trim().isEmpty()) {
                continue;
            }
            File file = new File(attachment);
            if (!file.isFile()) {
                throw new IOException("邮件附件不存在：" + attachment);
            }
            MimeBodyPart part = new MimeBodyPart();
            FileDataSource source = new FileDataSource(file);
            part.setDataHandler(new DataHandler(source));
            part.setFileName(file.getName());
            multipart.addBodyPart(part);
        }
        message.setContent(multipart);
        message.saveChanges();
        return message;
    }

    /**
     * 按兼容顺序打开插件 asset。
     */
    private static InputStream openPluginAsset(Context context, String name) {
        for (String candidate : new String[]{name, "plugins/" + name}) {
            try {
                return context.getAssets().open(candidate);
            } catch (IOException ignored) {
                // 当前候选不存在，继续下一兼容目录。
            }
        }
        return null;
    }

    /**
     * 复制流数据，不承担关闭职责。
     */
    private static void copy(InputStream inputStream, OutputStream outputStream)
            throws IOException {
        byte[] buffer = new byte[COPY_BUFFER_BYTES];
        int readCount;
        while ((readCount = inputStream.read(buffer)) != -1) {
            outputStream.write(buffer, 0, readCount);
        }
    }

    private static int readLittleEndianInt(byte[] value, int offset) {
        return (value[offset] & 0xff)
                | ((value[offset + 1] & 0xff) << 8)
                | ((value[offset + 2] & 0xff) << 16)
                | ((value[offset + 3] & 0xff) << 24);
    }

    private static boolean ensureOpenCvInitialized() {
        if (openCvInitialized) {
            return true;
        }
        synchronized (OPEN_CV_LOCK) {
            if (openCvInitialized) {
                return true;
            }
            openCvInitialized = OpenCVLoader.initLocal();
            if (!openCvInitialized) {
                Log.e(TAG, "OpenCV 初始化失败");
            }
            return openCvInitialized;
        }
    }
}
