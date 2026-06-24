package com.autolua.engine;

import android.content.Context;
import android.os.Build;
import android.util.Log;

import org.json.JSONArray;
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
 * 第一版只监听 127.0.0.1，PC 端通过 adb forward 访问。这样 VS Code 插件、
 * Qt 工具和命令行工具后续都能复用同一套 JSON-RPC 入口。
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

    private void startInternal() {
        if (running) {
            return;
        }

        port = EngineSettings.getHttpPort(appContext);
        running = true;
        Thread acceptThread = new Thread(this::runAcceptLoop, "EngineHttpServer");
        acceptThread.start();
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

        JSONObject call = new JSONObject(request.bodyText);
        Object id = call.has("id") ? call.opt("id") : JSONObject.NULL;
        String method = call.optString("method", "");
        JSONObject params = call.optJSONObject("params");
        if (params == null) {
            params = new JSONObject();
        }

        try {
            JSONObject result = handleJsonRpcMethod(method, params);
            return HttpResponse.json(200, makeJsonRpcResult(id, result));
        } catch (IllegalArgumentException exception) {
            return HttpResponse.json(200, makeJsonRpcError(id, -32602, exception.getMessage()));
        } catch (UnsupportedOperationException exception) {
            return HttpResponse.json(200, makeJsonRpcError(id, -32601, exception.getMessage()));
        } catch (Exception exception) {
            return HttpResponse.json(200, makeJsonRpcError(id, -32000, exception.getMessage()));
        }
    }

    private JSONObject handleJsonRpcMethod(String method, JSONObject params) throws JSONException {
        if ("device.info".equals(method)) {
            return makeDeviceInfo();
        }

        if ("device.setRootModeEnabled".equals(method)) {
            if (!params.has("enabled")) {
                throw new IllegalArgumentException("enabled is required");
            }

            boolean enabled = params.optBoolean("enabled", true);
            EngineSettings.setRootModeEnabled(appContext, enabled);
            return makeDeviceInfo();
        }

        if ("root.exec".equals(method)) {
            String command = params.optString("command", "");
            if (command.trim().isEmpty()) {
                throw new IllegalArgumentException("command is required");
            }

            int timeoutMs = params.optInt("timeoutMs", 2500);
            return makeRootExecResult(RootShellBridge.exec(command, timeoutMs));
        }

        if ("root.status".equals(method)) {
            return makeRootStatus(RootShellBridge.status());
        }

        if ("root.file.exists".equals(method)) {
            RootCommandResult rootResult = RootShellBridge.fileExists(requirePath(params));
            JSONObject result = new JSONObject();
            result.put("exists", rootResult.success);
            result.put("error", rootResult.error);
            return result;
        }

        if ("root.file.readText".equals(method)) {
            RootCommandResult rootResult = RootShellBridge.readTextFile(
                    requirePath(params),
                    params.optInt("timeoutMs", 2500)
            );
            if (!rootResult.success) {
                throw new IllegalStateException(resolveRootCommandError(rootResult, "root file read failed"));
            }

            JSONObject result = new JSONObject();
            result.put("content", rootResult.stdout);
            return result;
        }

        if ("root.file.writeText".equals(method)) {
            String content = params.optString("content", null);
            if (content == null) {
                throw new IllegalArgumentException("content is required");
            }

            RootCommandResult rootResult = RootShellBridge.writeTextFile(
                    requirePath(params),
                    content,
                    params.optInt("timeoutMs", 2500)
            );
            if (!rootResult.success) {
                throw new IllegalStateException(resolveRootCommandError(rootResult, "root file write failed"));
            }

            JSONObject result = new JSONObject();
            result.put("ok", true);
            return result;
        }

        if ("root.file.stat".equals(method)) {
            RootCommandResult rootResult = RootShellBridge.statFile(requirePath(params));
            if (!rootResult.success) {
                throw new IllegalStateException(resolveRootCommandError(rootResult, "root file stat failed"));
            }

            JSONObject result = new JSONObject();
            result.put("file", parseRootFileInfo(rootResult.stdout));
            return result;
        }

        if ("root.file.list".equals(method)) {
            RootCommandResult rootResult = RootShellBridge.listFiles(requirePath(params));
            if (!rootResult.success) {
                throw new IllegalStateException(resolveRootCommandError(rootResult, "root file list failed"));
            }

            JSONObject result = new JSONObject();
            result.put("entries", parseRootFileInfoArray(rootResult.stdout));
            return result;
        }

        if ("root.file.remove".equals(method)) {
            RootCommandResult rootResult = RootShellBridge.removeFile(
                    requirePath(params),
                    params.optBoolean("recursive", false)
            );
            if (!rootResult.success) {
                throw new IllegalStateException(resolveRootCommandError(rootResult, "root file remove failed"));
            }

            JSONObject result = new JSONObject();
            result.put("ok", true);
            return result;
        }

        if ("root.file.mkdir".equals(method)) {
            RootCommandResult rootResult = RootShellBridge.makeDirectory(
                    requirePath(params),
                    params.optBoolean("recursive", true)
            );
            if (!rootResult.success) {
                throw new IllegalStateException(resolveRootCommandError(rootResult, "root file mkdir failed"));
            }

            JSONObject result = new JSONObject();
            result.put("ok", true);
            return result;
        }

        if ("root.file.chmod".equals(method)) {
            String mode = params.optString("mode", "");
            if (mode.isEmpty()) {
                throw new IllegalArgumentException("mode is required");
            }

            RootCommandResult rootResult = RootShellBridge.chmod(requirePath(params), mode);
            if (!rootResult.success) {
                throw new IllegalStateException(resolveRootCommandError(rootResult, "root file chmod failed"));
            }

            JSONObject result = new JSONObject();
            result.put("ok", true);
            return result;
        }

        if ("root.file.chown".equals(method)) {
            String owner = params.optString("owner", "");
            if (owner.isEmpty()) {
                throw new IllegalArgumentException("owner is required");
            }

            RootCommandResult rootResult = RootShellBridge.chown(requirePath(params), owner);
            if (!rootResult.success) {
                throw new IllegalStateException(resolveRootCommandError(rootResult, "root file chown failed"));
            }

            JSONObject result = new JSONObject();
            result.put("ok", true);
            return result;
        }

        if ("root.process.pidOf".equals(method)) {
            RootCommandResult rootResult = RootShellBridge.pidOf(requireProcessName(params));
            if (!rootResult.success) {
                throw new IllegalStateException(resolveRootCommandError(rootResult, "root process pidOf failed"));
            }

            JSONObject result = new JSONObject();
            result.put("pids", parsePidArray(rootResult.stdout));
            return result;
        }

        if ("root.process.list".equals(method)) {
            RootCommandResult rootResult = RootShellBridge.listProcesses();
            if (!rootResult.success) {
                throw new IllegalStateException(resolveRootCommandError(rootResult, "root process list failed"));
            }

            JSONObject result = new JSONObject();
            result.put("processes", parseProcessArray(rootResult.stdout));
            return result;
        }

        if ("root.process.info".equals(method)) {
            RootCommandResult rootResult = RootShellBridge.processInfo(requireProcessTarget(params));
            if (!rootResult.success) {
                throw new IllegalStateException(resolveRootCommandError(rootResult, "root process info failed"));
            }

            JSONObject result = new JSONObject();
            result.put("processes", parseProcessArray(rootResult.stdout));
            return result;
        }

        if ("root.process.kill".equals(method)) {
            RootCommandResult rootResult = RootShellBridge.killProcess(
                    requireProcessTarget(params),
                    params.optInt("signal", 15)
            );
            if (!rootResult.success) {
                throw new IllegalStateException(resolveRootCommandError(rootResult, "root process kill failed"));
            }

            JSONObject result = new JSONObject();
            result.put("ok", true);
            return result;
        }

        if ("app.isInstalled".equals(method)) {
            String packageName = requirePackageName(params);
            JSONObject result = new JSONObject();
            result.put("installed", AndroidHostBridge.appIsInstalled(packageName));
            return result;
        }

        if ("app.open".equals(method) || "app.start".equals(method)) {
            String packageName = requirePackageName(params);
            JSONObject result = new JSONObject();
            result.put("ok", AndroidHostBridge.appOpen(packageName));
            return result;
        }

        if ("app.stop".equals(method)) {
            String packageName = requirePackageName(params);
            JSONObject result = new JSONObject();
            result.put("ok", AndroidHostBridge.appStop(packageName));
            return result;
        }

        if ("app.clearData".equals(method)) {
            String packageName = requirePackageName(params);
            JSONObject result = new JSONObject();
            result.put("ok", AndroidHostBridge.appClearData(packageName));
            return result;
        }

        if ("app.grant".equals(method)) {
            String packageName = requirePackageName(params);
            String permissionName = requirePermissionName(params);
            JSONObject result = new JSONObject();
            result.put("ok", AndroidHostBridge.appGrantPermission(packageName, permissionName));
            return result;
        }

        if ("app.revoke".equals(method)) {
            String packageName = requirePackageName(params);
            String permissionName = requirePermissionName(params);
            JSONObject result = new JSONObject();
            result.put("ok", AndroidHostBridge.appRevokePermission(packageName, permissionName));
            return result;
        }

        if ("app.current".equals(method)) {
            RootCommandResult rootResult = AndroidHostBridge.appCurrent();
            if (!rootResult.success) {
                throw new IllegalStateException(resolveRootCommandError(rootResult, "current app failed"));
            }
            return parseAppComponent(rootResult.stdout);
        }

        if ("app.install".equals(method)) {
            JSONObject result = new JSONObject();
            result.put("ok", AndroidHostBridge.appInstall(
                    requireApkPath(params),
                    params.optBoolean("replace", true)
            ));
            return result;
        }

        if ("app.uninstall".equals(method)) {
            String packageName = requirePackageName(params);
            JSONObject result = new JSONObject();
            result.put("ok", AndroidHostBridge.appUninstall(
                    packageName,
                    params.optBoolean("keepData", false)
            ));
            return result;
        }

        if ("app.disable".equals(method)) {
            String packageName = requirePackageName(params);
            JSONObject result = new JSONObject();
            result.put("ok", AndroidHostBridge.appDisable(packageName));
            return result;
        }

        if ("app.enable".equals(method)) {
            String packageName = requirePackageName(params);
            JSONObject result = new JSONObject();
            result.put("ok", AndroidHostBridge.appEnable(packageName));
            return result;
        }

        if ("key.press".equals(method)) {
            int keyCode = params.optInt("keyCode", -1);
            if (keyCode < 0) {
                throw new IllegalArgumentException("keyCode is required");
            }

            JSONObject result = new JSONObject();
            result.put("ok", AndroidHostBridge.keyPress(keyCode));
            return result;
        }

        if ("input.text".equals(method)) {
            String text = params.optString("text", null);
            if (text == null) {
                throw new IllegalArgumentException("text is required");
            }

            JSONObject result = new JSONObject();
            result.put("ok", AndroidHostBridge.inputText(text));
            return result;
        }

        if ("input.pasteText".equals(method)) {
            String text = params.optString("text", null);
            if (text == null) {
                throw new IllegalArgumentException("text is required");
            }

            JSONObject result = new JSONObject();
            result.put("ok", AndroidHostBridge.pasteText(text));
            return result;
        }

        if ("script.run".equals(method)) {
            String language = params.optString("language", "lua");
            if (!"lua".equals(language)) {
                throw new IllegalArgumentException("only lua is supported now");
            }

            String code = params.optString("code", null);
            if (code == null || code.isEmpty()) {
                throw new IllegalArgumentException("code is required");
            }

            String message = NativeEngine.runLuaText(code);
            JSONObject status = new JSONObject(NativeEngine.statusJson(0));
            JSONObject result = new JSONObject();
            result.put("taskId", status.optInt("taskId", 0));
            result.put("message", message);
            result.put("status", status.optString("status", "unknown"));
            return result;
        }

        if ("script.stop".equals(method)) {
            NativeEngine.stop();
            JSONObject result = new JSONObject();
            result.put("accepted", true);
            return result;
        }

        if ("script.pause".equals(method)) {
            JSONObject result = new JSONObject();
            result.put("accepted", NativeEngine.pause());
            result.put("status", new JSONObject(NativeEngine.statusJson(0)).optString("status", "unknown"));
            return result;
        }

        if ("script.resume".equals(method)) {
            JSONObject result = new JSONObject();
            result.put("accepted", NativeEngine.resume());
            result.put("status", new JSONObject(NativeEngine.statusJson(0)).optString("status", "unknown"));
            return result;
        }

        if ("log.drain".equals(method)) {
            int afterId = params.optInt("afterId", 0);
            JSONArray entries = new JSONArray(NativeEngine.drainLogs(afterId));
            JSONObject result = new JSONObject();
            result.put("entries", entries);
            result.put("lastId", entries.length() == 0
                    ? afterId
                    : entries.getJSONObject(entries.length() - 1).getInt("id"));
            return result;
        }

        if ("script.status".equals(method)) {
            int taskId = params.optInt("taskId", 0);
            return new JSONObject(NativeEngine.statusJson(taskId));
        }

        if ("screen.capture".equals(method)) {
            JSONObject capture = new JSONObject(NativeEngine.captureScreenJson());
            if (!capture.optBoolean("ok", false)) {
                throw new IllegalStateException(capture.optString("error", "screen capture failed"));
            }
            return capture.getJSONObject("image");
        }

        if ("image.release".equals(method)) {
            int imageId = params.optInt("id", 0);
            if (imageId <= 0) {
                imageId = params.optInt("imageId", 0);
            }

            if (imageId <= 0) {
                throw new IllegalArgumentException("image id is required");
            }

            if (!NativeEngine.releaseImage(imageId)) {
                throw new IllegalArgumentException("image handle is not found");
            }

            JSONObject result = new JSONObject();
            result.put("released", true);
            return result;
        }

        throw new UnsupportedOperationException("method is not found: " + method);
    }

    private JSONObject makeDeviceInfo() throws JSONException {
        boolean rootModeEnabled = EngineSettings.isRootModeEnabled(appContext);
        RootStatus rootStatus = RootShellBridge.status();
        boolean rootAvailable = rootStatus.available;
        boolean accessibilityEnabled = AutomationAccessibilityService.isEnabled();
        JSONObject result = new JSONObject();
        result.put("platform", "android");
        result.put("engineVersion", NativeEngine.engineVersion());
        result.put("luaVersion", NativeEngine.luaVersion());
        result.put("apiLevel", Build.VERSION.SDK_INT);
        result.put("packageName", appContext.getPackageName());
        result.put("rootModeEnabled", rootModeEnabled);
        result.put("rootAvailable", rootAvailable);
        result.put("rootStatus", makeRootStatus(rootStatus));
        result.put("accessibilityEnabled", accessibilityEnabled);
        result.put("automationMode", resolveAutomationMode(
                rootModeEnabled,
                rootAvailable,
                accessibilityEnabled
        ));
        result.put("httpHost", "127.0.0.1");
        result.put("httpPort", port);
        return result;
    }

    private static JSONObject makeRootStatus(RootStatus rootStatus) throws JSONException {
        JSONObject result = new JSONObject();
        result.put("available", rootStatus.available);
        result.put("commandMode", rootStatus.commandMode);
        result.put("suPath", rootStatus.suPath);
        result.put("cached", rootStatus.cached);
        result.put("cacheExpireAt", rootStatus.cacheExpireAt);
        result.put("error", rootStatus.error);

        JSONArray attempts = new JSONArray();
        for (RootStatus.ProbeAttempt attempt : rootStatus.attempts) {
            JSONObject item = new JSONObject();
            item.put("commandMode", attempt.commandMode);
            item.put("suPath", attempt.suPath);
            item.put("exitCode", attempt.exitCode);
            item.put("stdout", attempt.stdout);
            item.put("stderr", attempt.stderr);
            item.put("timedOut", attempt.timedOut);
            item.put("error", attempt.error);
            attempts.put(item);
        }
        result.put("attempts", attempts);
        return result;
    }

    private static String resolveAutomationMode(
            boolean rootModeEnabled,
            boolean rootAvailable,
            boolean accessibilityEnabled) {
        if (rootModeEnabled && rootAvailable) {
            return "root-first";
        }
        if (accessibilityEnabled) {
            return "accessibility";
        }
        return "none";
    }

    private static JSONObject makeRootExecResult(RootCommandResult rootResult) throws JSONException {
        JSONObject result = new JSONObject();
        result.put("ok", rootResult.success);
        result.put("exitCode", rootResult.exitCode);
        result.put("stdout", rootResult.stdout);
        result.put("stderr", rootResult.stderr);
        result.put("timedOut", rootResult.timedOut);
        result.put("error", rootResult.error);
        return result;
    }

    private static String resolveRootCommandError(RootCommandResult rootResult, String fallback) {
        if (!rootResult.error.isEmpty()) {
            return rootResult.error;
        }
        if (!rootResult.stderr.isEmpty()) {
            return rootResult.stderr;
        }
        return fallback;
    }

    private static String requirePath(JSONObject params) {
        String path = params.optString("path", "");
        if (path.isEmpty()) {
            throw new IllegalArgumentException("path is required");
        }
        return path;
    }

    private static String requireProcessName(JSONObject params) {
        String processName = params.optString("name", "");
        if (processName.trim().isEmpty()) {
            throw new IllegalArgumentException("name is required");
        }
        return processName;
    }

    private static String requireProcessTarget(JSONObject params) {
        String target = params.optString("target", "");
        if (target.trim().isEmpty()) {
            int pid = params.optInt("pid", 0);
            if (pid > 0) {
                return String.valueOf(pid);
            }
            String name = params.optString("name", "");
            if (!name.trim().isEmpty()) {
                return name;
            }
            throw new IllegalArgumentException("target, pid or name is required");
        }
        return target;
    }

    private static JSONArray parsePidArray(String stdout) {
        JSONArray pids = new JSONArray();
        if (stdout == null || stdout.trim().isEmpty()) {
            return pids;
        }

        String[] parts = stdout.trim().split("\\s+");
        for (String part : parts) {
            try {
                pids.put(Integer.parseInt(part));
            } catch (NumberFormatException ignored) {
                // pidof 输出里出现非数字时跳过，避免单个异常值影响整个响应。
            }
        }
        return pids;
    }

    private static JSONArray parseRootFileInfoArray(String stdout) throws JSONException {
        JSONArray entries = new JSONArray();
        if (stdout == null || stdout.trim().isEmpty()) {
            return entries;
        }

        String[] lines = stdout.split("\\r?\\n");
        for (String line : lines) {
            JSONObject file = parseRootFileInfoOrNull(line);
            if (file != null) {
                entries.put(file);
            }
        }
        return entries;
    }

    private static JSONObject parseRootFileInfo(String stdout) throws JSONException {
        JSONObject file = parseRootFileInfoOrNull(stdout);
        if (file == null) {
            throw new IllegalStateException("root file stat output is invalid");
        }
        return file;
    }

    private static JSONObject parseRootFileInfoOrNull(String line) throws JSONException {
        if (line == null || line.trim().isEmpty()) {
            return null;
        }

        String[] parts = line.trim().split("\\|", 9);
        if (parts.length != 9) {
            return null;
        }

        try {
            JSONObject file = new JSONObject();
            file.put("type", parts[0]);
            file.put("size", Long.parseLong(parts[1]));
            file.put("mode", parts[2]);
            file.put("user", parts[3]);
            file.put("group", parts[4]);
            file.put("uid", Integer.parseInt(parts[5]));
            file.put("gid", Integer.parseInt(parts[6]));
            file.put("modifiedAt", Long.parseLong(parts[7]));
            file.put("path", parts[8]);
            file.put("name", rootFileBaseName(parts[8]));
            return file;
        } catch (NumberFormatException ignored) {
            return null;
        }
    }

    private static String rootFileBaseName(String path) {
        int index = path.lastIndexOf('/');
        if (index < 0) {
            return path;
        }
        if (index + 1 >= path.length()) {
            return "";
        }
        return path.substring(index + 1);
    }

    private static JSONObject parseAppComponent(String text) throws JSONException {
        String component = text == null ? "" : text.trim();
        int separator = component.indexOf('/');
        if (separator <= 0 || separator >= component.length() - 1) {
            throw new IllegalStateException("current app output is invalid");
        }

        JSONObject result = new JSONObject();
        result.put("component", component);
        result.put("packageName", component.substring(0, separator));
        result.put("activityName", component.substring(separator + 1));
        return result;
    }

    private static JSONArray parseProcessArray(String stdout) throws JSONException {
        JSONArray processes = new JSONArray();
        if (stdout == null || stdout.trim().isEmpty()) {
            return processes;
        }

        String[] lines = stdout.split("\\r?\\n");
        for (String line : lines) {
            JSONObject process = parseProcessLine(line);
            if (process != null) {
                processes.put(process);
            }
        }
        return processes;
    }

    private static JSONObject parseProcessLine(String line) throws JSONException {
        if (line == null || line.trim().isEmpty()) {
            return null;
        }

        String trimmed = line.trim();
        if (trimmed.startsWith("PID ") || trimmed.contains(" PID ")) {
            return null;
        }

        String[] parts = trimmed.split("\\s+", 5);
        if (parts.length < 4) {
            return null;
        }

        try {
            JSONObject process = new JSONObject();
            process.put("pid", Integer.parseInt(parts[0]));
            process.put("ppid", Integer.parseInt(parts[1]));
            process.put("user", parts[2]);
            process.put("name", parts[3]);
            process.put("args", parts.length >= 5 ? parts[4] : "");
            return process;
        } catch (NumberFormatException ignored) {
            // 不符合明确列格式的行直接跳过，避免设备差异导致整个响应失败。
            return null;
        }
    }

    private static String requirePackageName(JSONObject params) {
        String packageName = params.optString("packageName", "");
        if (packageName.trim().isEmpty()) {
            throw new IllegalArgumentException("packageName is required");
        }
        return packageName;
    }

    private static String requirePermissionName(JSONObject params) {
        String permissionName = params.optString("permission", "");
        if (permissionName.trim().isEmpty()) {
            throw new IllegalArgumentException("permission is required");
        }
        return permissionName;
    }

    private static String requireApkPath(JSONObject params) {
        String path = params.optString("path", "");
        if (path.trim().isEmpty()) {
            path = params.optString("apkPath", "");
        }
        if (path.trim().isEmpty()) {
            throw new IllegalArgumentException("path is required");
        }
        return path;
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

    private static JSONObject makeJsonRpcResult(Object id, JSONObject result) throws JSONException {
        JSONObject response = new JSONObject();
        response.put("jsonrpc", "2.0");
        response.put("id", id);
        response.put("result", result);
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
            // 关闭服务时不需要把 socket 关闭异常暴露给脚本层。
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
