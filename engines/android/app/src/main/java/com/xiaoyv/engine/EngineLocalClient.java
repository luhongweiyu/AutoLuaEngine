/**
 * 文件用途：主进程访问本机 EngineHttpServer 的轻量 JSON-RPC 客户端。
 */
package com.xiaoyv.engine;

import android.content.Context;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;

/**
 * App 主进程访问本机引擎 HTTP 服务的小客户端。
 *
 * 这个类用于把 MainActivity 逐步改成“控制端”：它通过 JSON-RPC 查询日志和状态，
 * 而不是直接调用 NativeEngine。后续 EngineService 拆到 :engine 独立进程后，这条
 * 路径仍然成立。
 */
public final class EngineLocalClient {
    private static final int CONNECT_TIMEOUT_MS = 1000;
    private static final int READ_TIMEOUT_MS = 3000;

    private EngineLocalClient() {
    }

    public static JSONObject call(Context context, String method, JSONObject params)
            throws IOException, JSONException {
        JSONObject request = new JSONObject();
        request.put("jsonrpc", "2.0");
        request.put("id", 1);
        request.put("method", method);
        request.put("params", params == null ? new JSONObject() : params);

        String url = "http://127.0.0.1:"
                + EngineSettings.getHttpPort(context)
                + "/jsonrpc";
        JSONObject response = new JSONObject(postJson(url, request.toString()));
        if (response.has("error")) {
            JSONObject error = response.optJSONObject("error");
            String message = error == null
                    ? "JSON-RPC 调用失败"
                    : error.optString("message", "JSON-RPC 调用失败");
            throw new IOException(message);
        }

        JSONObject result = response.optJSONObject("result");
        return result == null ? new JSONObject() : result;
    }

    private static String postJson(String url, String body) throws IOException {
        HttpURLConnection connection = (HttpURLConnection) new URL(url).openConnection();
        connection.setConnectTimeout(CONNECT_TIMEOUT_MS);
        connection.setReadTimeout(READ_TIMEOUT_MS);
        connection.setRequestMethod("POST");
        connection.setDoOutput(true);
        connection.setRequestProperty("Content-Type", "application/json; charset=utf-8");

        byte[] bodyBytes = body.getBytes(StandardCharsets.UTF_8);
        connection.setRequestProperty("Content-Length", String.valueOf(bodyBytes.length));
        try (OutputStream outputStream = connection.getOutputStream()) {
            outputStream.write(bodyBytes);
        }

        int statusCode = connection.getResponseCode();
        InputStream inputStream = statusCode >= 200 && statusCode < 300
                ? connection.getInputStream()
                : connection.getErrorStream();
        String responseText = readAllText(inputStream);
        connection.disconnect();

        if (statusCode < 200 || statusCode >= 300) {
            throw new IOException("HTTP 请求失败（状态码 " + statusCode + "）：" + responseText);
        }
        return responseText;
    }

    private static String readAllText(InputStream inputStream) throws IOException {
        if (inputStream == null) {
            return "";
        }

        try (InputStream source = inputStream;
             ByteArrayOutputStream outputStream = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[4096];
            int readCount;
            while ((readCount = source.read(buffer)) != -1) {
                outputStream.write(buffer, 0, readCount);
            }
            return outputStream.toString(StandardCharsets.UTF_8.name());
        }
    }
}
