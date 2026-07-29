/**
 * 文件用途：实现脚本 HTTP、文件传输与 WebSocket 平台能力。
 */
package com.xiaoyv.engine;

import android.util.Base64;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.Iterator;
import java.util.Locale;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;

import okhttp3.Headers;
import okhttp3.MediaType;
import okhttp3.MultipartBody;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.RequestBody;
import okhttp3.Response;
import okhttp3.ResponseBody;
import okhttp3.WebSocket;
import okhttp3.WebSocketListener;
import okio.ByteString;

final class NetworkPlatformBridge {
    private static final OkHttpClient CLIENT = new OkHttpClient.Builder()
            .followRedirects(true)
            .followSslRedirects(true)
            .build();
    private static final AtomicLong NEXT_WEB_SOCKET_ID = new AtomicLong(1);
    private static final ConcurrentHashMap<Long, WebSocketSession> WEB_SOCKETS =
            new ConcurrentHashMap<>();

    private NetworkPlatformBridge() {
    }

    /**
     * 脚本生命周期结束时立即释放尚未关闭的 WebSocket。
     *
     * 会话表属于 :engine 进程，不能让上一个脚本的连接和回调事件泄漏到下一次运行。
     */
    static void closeAllWebSockets() {
        WebSocketSession[] sessions = WEB_SOCKETS.values().toArray(new WebSocketSession[0]);
        WEB_SOCKETS.clear();
        for (WebSocketSession session : sessions) {
            session.cancel();
        }
    }

    static Object call(String operation, JSONObject arguments) throws Exception {
        switch (operation) {
            case "network.request":
                return request(arguments);
            case "network.download":
                return download(arguments);
            case "network.upload":
                return upload(arguments);
            case "network.websocket.start":
                return startWebSocket(arguments);
            case "network.websocket.poll":
                return pollWebSocket(arguments.getLong("handle"));
            case "network.websocket.send":
                return sendWebSocket(arguments);
            case "network.websocket.close":
                return closeWebSocket(arguments);
            default:
                throw new IllegalArgumentException("不支持的网络能力：" + operation);
        }
    }

    private static JSONObject request(JSONObject arguments) throws Exception {
        String url = requireText(arguments, "url");
        String method = arguments.optString("method", "GET").toUpperCase(Locale.ROOT);
        byte[] body = decode(arguments.optString("body", ""));
        Request.Builder builder = new Request.Builder().url(url);
        applyHeaders(builder, arguments.optJSONObject("headers"));

        if ("GET".equals(method) || "HEAD".equals(method)) {
            builder.method(method, null);
        } else {
            String contentType = arguments.optString(
                    "contentType",
                    "application/octet-stream"
            );
            builder.method(method, RequestBody.create(MediaType.parse(contentType), body));
        }
        return execute(builder.build(), timeoutSeconds(arguments));
    }

    private static JSONObject download(JSONObject arguments) throws Exception {
        String url = requireText(arguments, "url");
        File target = new File(requireText(arguments, "path"));
        File parent = target.getParentFile();
        if (parent != null && !parent.exists() && !parent.mkdirs()) {
            throw new IllegalArgumentException("无法创建下载目录：" + parent);
        }

        Request.Builder builder = new Request.Builder().url(url);
        applyHeaders(builder, arguments.optJSONObject("headers"));
        OkHttpClient client = timedClient(timeoutSeconds(arguments));
        File temporary = new File(target.getAbsolutePath() + ".part." + System.nanoTime());
        try (Response response = client.newCall(builder.build()).execute()) {
            ResponseBody body = response.body();
            if (!response.isSuccessful() || body == null) {
                throw new IOException("下载失败，HTTP " + response.code());
            }
            try (java.io.InputStream input = body.byteStream();
                 FileOutputStream output = new FileOutputStream(temporary, false)) {
                byte[] buffer = new byte[32 * 1024];
                int count;
                while ((count = input.read(buffer)) != -1) {
                    output.write(buffer, 0, count);
                }
            }
            replaceFile(temporary, target);
            JSONObject result = responseMetadata(response);
            result.put("path", target.getAbsolutePath());
            result.put("bytes", target.length());
            return result;
        } finally {
            if (temporary.exists()) {
                //noinspection ResultOfMethodCallIgnored
                temporary.delete();
            }
        }
    }

    private static JSONObject upload(JSONObject arguments) throws Exception {
        String url = requireText(arguments, "url");
        File file = new File(requireText(arguments, "path"));
        if (!file.isFile()) {
            throw new IllegalArgumentException("上传文件不存在：" + file);
        }

        MediaType mediaType = MediaType.parse(
                arguments.optString("contentType", "application/octet-stream")
        );
        RequestBody fileBody = RequestBody.create(mediaType, file);
        RequestBody multipart = new MultipartBody.Builder()
                .setType(MultipartBody.FORM)
                .addFormDataPart(
                        arguments.optString("fieldName", "file"),
                        file.getName(),
                        fileBody
                )
                .build();
        Request.Builder builder = new Request.Builder().url(url).post(multipart);
        applyHeaders(builder, arguments.optJSONObject("headers"));
        return execute(builder.build(), timeoutSeconds(arguments));
    }

    private static JSONObject execute(Request request, int timeoutSeconds) throws Exception {
        try (Response response = timedClient(timeoutSeconds).newCall(request).execute()) {
            JSONObject result = responseMetadata(response);
            ResponseBody body = response.body();
            result.put(
                    "body",
                    encode(body == null ? new byte[0] : body.bytes())
            );
            return result;
        }
    }

    private static JSONObject responseMetadata(Response response) throws JSONException {
        JSONObject result = new JSONObject();
        result.put("code", response.code());
        result.put("message", response.message());
        JSONObject headers = new JSONObject();
        Headers responseHeaders = response.headers();
        for (String name : responseHeaders.names()) {
            headers.put(name, joinHeaderValues(responseHeaders.values(name)));
        }
        result.put("headers", headers);
        return result;
    }

    private static long startWebSocket(JSONObject arguments) {
        Request.Builder builder = new Request.Builder().url(requireText(arguments, "url"));
        applyHeaders(builder, arguments.optJSONObject("headers"));
        Request request = builder.build();

        long handle = NEXT_WEB_SOCKET_ID.getAndIncrement();
        WebSocketSession session = new WebSocketSession(handle);
        WEB_SOCKETS.put(handle, session);
        try {
            session.socket = CLIENT.newWebSocket(request, session);
            return handle;
        } catch (RuntimeException exception) {
            WEB_SOCKETS.remove(handle, session);
            session.cancel();
            throw exception;
        }
    }

    private static JSONObject pollWebSocket(long handle) throws JSONException {
        WebSocketSession session = WEB_SOCKETS.get(handle);
        JSONObject result = new JSONObject();
        JSONArray events = new JSONArray();
        if (session == null) {
            result.put("events", events);
            result.put("terminal", true);
            return result;
        }
        for (int index = 0; index < 64; index++) {
            JSONObject event = session.events.poll();
            if (event == null) {
                break;
            }
            events.put(event);
        }
        boolean terminal = session.terminal && session.events.isEmpty();
        if (terminal) {
            WEB_SOCKETS.remove(handle, session);
        }
        result.put("events", events);
        result.put("terminal", terminal);
        return result;
    }

    private static boolean sendWebSocket(JSONObject arguments) {
        WebSocketSession session = WEB_SOCKETS.get(arguments.optLong("handle", 0));
        return session != null
                && session.socket != null
                && session.socket.send(arguments.optString("text", ""));
    }

    private static boolean closeWebSocket(JSONObject arguments) {
        WebSocketSession session = WEB_SOCKETS.get(arguments.optLong("handle", 0));
        if (session == null || session.socket == null) {
            return false;
        }
        return session.socket.close(
                arguments.optInt("code", 1000),
                arguments.optString("reason", "")
        );
    }

    private static void applyHeaders(Request.Builder builder, JSONObject headers) {
        if (headers == null) {
            return;
        }
        for (Iterator<String> iterator = headers.keys(); iterator.hasNext();) {
            String name = iterator.next();
            Object value = headers.opt(name);
            if (value != null && value != JSONObject.NULL) {
                builder.header(name, String.valueOf(value));
            }
        }
    }

    private static OkHttpClient timedClient(int timeoutSeconds) {
        return CLIENT.newBuilder()
                .callTimeout(timeoutSeconds, TimeUnit.SECONDS)
                .connectTimeout(timeoutSeconds, TimeUnit.SECONDS)
                .readTimeout(timeoutSeconds, TimeUnit.SECONDS)
                .writeTimeout(timeoutSeconds, TimeUnit.SECONDS)
                .build();
    }

    private static int timeoutSeconds(JSONObject arguments) {
        int value = arguments.optInt("timeout", 30);
        return Math.max(1, Math.min(value, 24 * 60 * 60));
    }

    private static String requireText(JSONObject arguments, String name) {
        String value = arguments.optString(name, "");
        if (value.isEmpty()) {
            throw new IllegalArgumentException(name + " 参数不能为空");
        }
        return value;
    }

    private static String joinHeaderValues(java.util.List<String> values) {
        StringBuilder result = new StringBuilder();
        for (String value : values) {
            if (result.length() > 0) {
                result.append(", ");
            }
            result.append(value);
        }
        return result.toString();
    }

    /**
     * 用同目录临时文件替换目标。Android 8+ 使用带覆盖语义的 Files.move；旧系统先把
     * 原目标移到备份，若新文件切换失败则恢复，避免下载失败把用户旧文件直接删掉。
     */
    private static void replaceFile(File temporary, File target) throws IOException {
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.O) {
            try {
                java.nio.file.Files.move(
                        temporary.toPath(),
                        target.toPath(),
                        java.nio.file.StandardCopyOption.REPLACE_EXISTING,
                        java.nio.file.StandardCopyOption.ATOMIC_MOVE
                );
                return;
            } catch (java.nio.file.AtomicMoveNotSupportedException ignored) {
                java.nio.file.Files.move(
                        temporary.toPath(),
                        target.toPath(),
                        java.nio.file.StandardCopyOption.REPLACE_EXISTING
                );
                return;
            }
        }

        File backup = new File(target.getAbsolutePath() + ".backup." + System.nanoTime());
        boolean hadTarget = target.exists();
        if (hadTarget && !target.renameTo(backup)) {
            throw new IOException("无法备份原下载目标：" + target);
        }
        if (temporary.renameTo(target)) {
            if (hadTarget && backup.exists() && !backup.delete()) {
                // 新目标已经完整就位；残留备份可由用户恢复，不能因此把成功下载判为失败。
                backup.deleteOnExit();
            }
            return;
        }
        if (hadTarget && !backup.renameTo(target)) {
            throw new IOException("下载文件替换失败，原文件保留在：" + backup);
        }
        throw new IOException("无法完成下载文件替换：" + target);
    }

    private static String encode(byte[] value) {
        return Base64.encodeToString(value, Base64.NO_WRAP);
    }

    private static byte[] decode(String value) {
        if (value == null || value.isEmpty()) {
            return new byte[0];
        }
        return Base64.decode(value, Base64.DEFAULT);
    }

    private static JSONObject event(String type) {
        JSONObject event = new JSONObject();
        try {
            event.put("type", type);
        } catch (JSONException ignored) {
            // JSONObject 写入常量键和值不会失败。
        }
        return event;
    }

    private static final class WebSocketSession extends WebSocketListener {
        private final long handle;
        private final ConcurrentLinkedQueue<JSONObject> events =
                new ConcurrentLinkedQueue<>();
        private volatile WebSocket socket;
        private volatile boolean terminal;

        private WebSocketSession(long handle) {
            this.handle = handle;
        }

        private void cancel() {
            terminal = true;
            events.clear();
            WebSocket current = socket;
            socket = null;
            if (current != null) {
                current.cancel();
            }
        }

        @Override
        public void onOpen(WebSocket webSocket, Response response) {
            JSONObject value = event("open");
            try {
                value.put("handle", handle);
                value.put("code", response.code());
            } catch (JSONException ignored) {
                // 常量 JSON 字段不会失败。
            }
            events.add(value);
        }

        @Override
        public void onMessage(WebSocket webSocket, String text) {
            JSONObject value = event("message");
            try {
                value.put("text", text);
                value.put("binary", false);
            } catch (JSONException ignored) {
                // 常量 JSON 字段不会失败。
            }
            events.add(value);
        }

        @Override
        public void onMessage(WebSocket webSocket, ByteString bytes) {
            JSONObject value = event("message");
            try {
                value.put("text", encode(bytes.toByteArray()));
                value.put("binary", true);
            } catch (JSONException ignored) {
                // 常量 JSON 字段不会失败。
            }
            events.add(value);
        }

        @Override
        public void onClosing(WebSocket webSocket, int code, String reason) {
            // RFC 6455 允许对端发送不带状态码的关闭帧；OkHttp 用仅供本地表示的
            // 保留码 1005 回调这种情况，但 1005 不能出现在实际关闭帧中。
            int responseCode = code == 1005 ? 1000 : code;
            webSocket.close(responseCode, code == 1005 ? "" : reason);
        }

        @Override
        public void onClosed(WebSocket webSocket, int code, String reason) {
            JSONObject value = event("close");
            try {
                value.put("code", code);
                value.put("reason", reason);
            } catch (JSONException ignored) {
                // 常量 JSON 字段不会失败。
            }
            events.add(value);
            terminal = true;
        }

        @Override
        public void onFailure(WebSocket webSocket, Throwable throwable, Response response) {
            JSONObject value = event("error");
            try {
                value.put(
                        "message",
                        throwable == null ? "WebSocket 连接失败" : String.valueOf(throwable.getMessage())
                );
                value.put("code", response == null ? 0 : response.code());
            } catch (JSONException ignored) {
                // 常量 JSON 字段不会失败。
            }
            events.add(value);
            terminal = true;
        }
    }
}
