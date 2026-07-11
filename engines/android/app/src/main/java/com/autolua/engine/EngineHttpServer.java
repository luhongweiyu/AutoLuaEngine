/**
 * 文件用途：在 :engine 进程内提供 HTTP JSON-RPC 服务，供 IDE/PC 调试控制引擎。
 */
package com.autolua.engine;

import android.content.Context;
import android.content.Intent;
import android.util.Log;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.Closeable;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Android 引擎本地 HTTP 服务。
 *
 * 这个类只负责 127.0.0.1 HTTP、JSON-RPC 包装和 UI 状态广播。具体命令校验、
 * 脚本任务控制、root/截图/设备/应用 API 调度都统一交给 libengine.so，避免
 * Java 层和 native 层各维护一套业务分发。
 */
public final class EngineHttpServer {
    private static final String TAG = "AutoLuaEngine";
    private static final Object LOCK = new Object();
    private static final int MAX_BODY_BYTES = 1024 * 1024;

    private static EngineHttpServer instance;

    private final Context appContext;
    private final ExecutorService clientExecutor = Executors.newCachedThreadPool();

    private int port;
    private volatile boolean running;
    private ServerSocket serverSocket;

    private EngineHttpServer(Context context) {
        appContext = context.getApplicationContext();
    }

    public static void start(Context context) {
        synchronized (LOCK) {
            if (instance == null) {
                instance = new EngineHttpServer(context);
            }
            instance.startInternal();
        }
    }

    public static void stop() {
        synchronized (LOCK) {
            if (instance == null) {
                return;
            }
            instance.stopInternal();
            instance = null;
        }
    }

    private void startInternal() {
        if (running) {
            return;
        }

        port = EngineSettings.getHttpPort(appContext);
        running = true;
        Thread acceptThread = new Thread(this::runAcceptLoop, "EngineHttpServer");
        acceptThread.start();
    }

    private void stopInternal() {
        running = false;
        closeQuietly(serverSocket);
        serverSocket = null;
        clientExecutor.shutdownNow();
    }

    private void runAcceptLoop() {
        try {
            InetAddress loopback = InetAddress.getByName("127.0.0.1");
            serverSocket = new ServerSocket(port, 50, loopback);
            Log.i(TAG, "engine http server started on 127.0.0.1:" + port);

            while (running) {
                Socket socket = serverSocket.accept();
                clientExecutor.execute(() -> handleClient(socket));
            }
        } catch (IOException exception) {
            if (running) {
                Log.e(TAG, "engine http server failed: " + exception.getMessage());
            }
        } finally {
            running = false;
            closeQuietly(serverSocket);
            serverSocket = null;
        }
    }

    private void handleClient(Socket socket) {
        try (Socket client = socket) {
            HttpRequest request = readRequest(client.getInputStream());
            HttpResponse response = handleRequest(request);
            writeResponse(client.getOutputStream(), response);
        } catch (Exception exception) {
            Log.e(TAG, "handle http request failed: " + exception.getMessage());
        }
    }

    private HttpResponse handleRequest(HttpRequest request) throws JSONException {
        if ("GET".equals(request.method) && "/health".equals(request.path)) {
            JSONObject result = new JSONObject();
            result.put("ok", true);
            result.put("port", port);
            return HttpResponse.json(200, result);
        }

        if (!"POST".equals(request.method) || !"/jsonrpc".equals(request.path)) {
            return HttpResponse.json(404, makePlainError("not found"));
        }

        Object id = JSONObject.NULL;
        String method = "";
        try {
            JSONObject call = new JSONObject(request.bodyText);
            id = call.has("id") ? call.opt("id") : JSONObject.NULL;
            method = call.optString("method", "");
            String paramsJson = readParamsJson(call);

            broadcastBeforeNativeCommand(method);
            JSONObject nativeEnvelope = new JSONObject(NativeEngine.callJson(method, paramsJson));
            if (!nativeEnvelope.optBoolean("ok", false)) {
                broadcastNativeCommandError(
                        method,
                        nativeEnvelope.optString("error", "native command failed")
                );
                return HttpResponse.json(
                        200,
                        makeJsonRpcError(
                                id,
                                nativeEnvelope.optInt("code", -32000),
                                nativeEnvelope.optString("error", "native command failed")
                        )
                );
            }

            Object result = nativeEnvelope.has("result")
                    ? nativeEnvelope.opt("result")
                    : JSONObject.NULL;
            broadcastAfterNativeCommand(method, result);
            return HttpResponse.json(200, makeJsonRpcResult(id, result));
        } catch (JSONException exception) {
            broadcastNativeCommandError(method, exception.getMessage());
            return HttpResponse.json(200, makeJsonRpcError(id, -32600, exception.getMessage()));
        } catch (IllegalArgumentException exception) {
            broadcastNativeCommandError(method, exception.getMessage());
            return HttpResponse.json(200, makeJsonRpcError(id, -32602, exception.getMessage()));
        } catch (Exception exception) {
            broadcastNativeCommandError(method, exception.getMessage());
            return HttpResponse.json(200, makeJsonRpcError(id, -32000, exception.getMessage()));
        }
    }

    private static String readParamsJson(JSONObject call) {
        Object params = call.opt("params");
        if (params == null || params == JSONObject.NULL) {
            return "{}";
        }
        if (!(params instanceof JSONObject)) {
            throw new IllegalArgumentException("params must be an object");
        }
        return params.toString();
    }

    private void broadcastBeforeNativeCommand(String method) {
        if ("script.run".equals(method)) {
            broadcastStatus(EngineService.STATE_RUNNING, "脚本运行中：IDE/HTTP");
        }
    }

    private void broadcastAfterNativeCommand(String method, Object result) {
        if ("script.run".equals(method) && result instanceof JSONObject) {
            JSONObject scriptResult = (JSONObject) result;
            String status = scriptResult.optString("status", "unknown");
            String state = "finished".equals(status)
                    ? EngineService.STATE_FINISHED
                    : EngineService.STATE_FAILED;
            broadcastStatus(state, scriptResult.optString("message", "脚本执行完成"));
            return;
        }

        if ("script.stop".equals(method) && result instanceof JSONObject) {
            JSONObject stopResult = (JSONObject) result;
            boolean accepted = stopResult.optBoolean("accepted", false);
            String status = stopResult.optString("status", "unknown");
            if (EngineService.STATE_STOPPING.equals(status)) {
                broadcastStatus(
                        EngineService.STATE_STOPPING,
                        accepted ? "已请求停止脚本" : "脚本正在停止"
                );
            } else {
                broadcastStatus(EngineService.STATE_FINISHED, "当前没有运行脚本");
            }
            return;
        }

        if ("script.pause".equals(method) && result instanceof JSONObject) {
            boolean accepted = ((JSONObject) result).optBoolean("accepted", false);
            broadcastStatus(
                    accepted ? EngineService.STATE_PAUSING : EngineService.STATE_FAILED,
                    accepted ? "已请求暂停脚本" : "当前没有可暂停的脚本"
            );
            return;
        }

        if ("script.resume".equals(method) && result instanceof JSONObject) {
            boolean accepted = ((JSONObject) result).optBoolean("accepted", false);
            broadcastStatus(
                    accepted ? EngineService.STATE_RUNNING : EngineService.STATE_FAILED,
                    accepted ? "已请求继续脚本" : "当前没有已暂停的脚本"
            );
        }
    }

    private void broadcastNativeCommandError(String method, String message) {
        if ("script.run".equals(method)) {
            broadcastStatus(EngineService.STATE_FAILED, "脚本运行失败：" + message);
        }
    }

    private void broadcastStatus(String state, String message) {
        Intent intent = new Intent(EngineService.ACTION_STATUS);
        intent.setPackage(appContext.getPackageName());
        intent.putExtra(EngineService.EXTRA_STATE, state);
        intent.putExtra(EngineService.EXTRA_MESSAGE, message == null ? "" : message);
        appContext.sendBroadcast(intent);
    }

    private static HttpRequest readRequest(InputStream inputStream) throws IOException {
        byte[] headerBytes = readHeaderBytes(inputStream);
        String headerText = new String(headerBytes, StandardCharsets.ISO_8859_1);
        String[] lines = headerText.split("\\r?\\n");
        if (lines.length == 0) {
            throw new IOException("empty http request");
        }

        String[] requestParts = lines[0].split(" ");
        if (requestParts.length < 2) {
            throw new IOException("invalid http request line");
        }

        Map<String, String> headers = new HashMap<>();
        for (int i = 1; i < lines.length; i++) {
            int splitIndex = lines[i].indexOf(':');
            if (splitIndex <= 0) {
                continue;
            }

            String name = lines[i].substring(0, splitIndex).trim().toLowerCase(Locale.US);
            String value = lines[i].substring(splitIndex + 1).trim();
            headers.put(name, value);
        }

        int contentLength = parseContentLength(headers);
        if (contentLength > MAX_BODY_BYTES) {
            throw new IOException("request body is too large");
        }

        byte[] bodyBytes = readExactBytes(inputStream, contentLength);
        return new HttpRequest(
                requestParts[0],
                requestParts[1],
                new String(bodyBytes, StandardCharsets.UTF_8)
        );
    }

    private static byte[] readHeaderBytes(InputStream inputStream) throws IOException {
        ByteArrayOutputStream outputStream = new ByteArrayOutputStream();
        int matched = 0;
        int value;

        while ((value = inputStream.read()) != -1) {
            outputStream.write(value);

            if ((matched == 0 && value == '\r')
                    || (matched == 1 && value == '\n')
                    || (matched == 2 && value == '\r')
                    || (matched == 3 && value == '\n')) {
                matched++;
                if (matched == 4) {
                    return outputStream.toByteArray();
                }
            } else {
                matched = value == '\r' ? 1 : 0;
            }

            if (outputStream.size() > 16 * 1024) {
                throw new IOException("http headers are too large");
            }
        }

        throw new IOException("http header is incomplete");
    }

    private static int parseContentLength(Map<String, String> headers) throws IOException {
        String value = headers.get("content-length");
        if (value == null || value.isEmpty()) {
            return 0;
        }

        try {
            return Integer.parseInt(value);
        } catch (NumberFormatException exception) {
            throw new IOException("invalid content-length");
        }
    }

    private static byte[] readExactBytes(InputStream inputStream, int length) throws IOException {
        byte[] buffer = new byte[length];
        int offset = 0;

        while (offset < length) {
            int readCount = inputStream.read(buffer, offset, length - offset);
            if (readCount == -1) {
                throw new IOException("http body is incomplete");
            }
            offset += readCount;
        }

        return buffer;
    }

    private static void writeResponse(OutputStream outputStream, HttpResponse response)
            throws IOException {
        byte[] bodyBytes = response.body.getBytes(StandardCharsets.UTF_8);
        String header = "HTTP/1.1 " + response.statusCode + " " + response.reason + "\r\n"
                + "Content-Type: application/json; charset=utf-8\r\n"
                + "Content-Length: " + bodyBytes.length + "\r\n"
                + "Connection: close\r\n"
                + "\r\n";

        outputStream.write(header.getBytes(StandardCharsets.ISO_8859_1));
        outputStream.write(bodyBytes);
        outputStream.flush();
    }

    private static JSONObject makePlainError(String message) throws JSONException {
        JSONObject result = new JSONObject();
        result.put("error", message);
        return result;
    }

    private static JSONObject makeJsonRpcResult(Object id, Object result) throws JSONException {
        JSONObject response = new JSONObject();
        response.put("jsonrpc", "2.0");
        response.put("id", id);
        response.put("result", result == null ? JSONObject.NULL : result);
        return response;
    }

    private static JSONObject makeJsonRpcError(Object id, int code, String message)
            throws JSONException {
        JSONObject error = new JSONObject();
        error.put("code", code);
        error.put("message", message == null ? "unknown error" : message);

        JSONObject response = new JSONObject();
        response.put("jsonrpc", "2.0");
        response.put("id", id);
        response.put("error", error);
        return response;
    }

    private static void closeQuietly(Closeable closeable) {
        if (closeable == null) {
            return;
        }

        try {
            closeable.close();
        } catch (IOException ignored) {
            // 关闭服务时 socket 已经进入回收流程，这里不再向脚本层暴露关闭异常。
        }
    }

    private static final class HttpRequest {
        private final String method;
        private final String path;
        private final String bodyText;

        private HttpRequest(String method, String path, String bodyText) {
            this.method = method;
            this.path = path;
            this.bodyText = bodyText;
        }
    }

    private static final class HttpResponse {
        private final int statusCode;
        private final String reason;
        private final String body;

        private HttpResponse(int statusCode, String reason, String body) {
            this.statusCode = statusCode;
            this.reason = reason;
            this.body = body;
        }

        private static HttpResponse json(int statusCode, JSONObject body) {
            String reason = statusCode == 200 ? "OK" : "Not Found";
            return new HttpResponse(statusCode, reason, body.toString());
        }
    }
}
