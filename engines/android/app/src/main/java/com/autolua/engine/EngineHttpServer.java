package com.autolua.engine;

import android.content.Context;
import android.content.Intent;
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
            if (enabled) {
                RootStatus rootStatus = RootShellBridge.prepareRootRuntime();
                if (rootStatus.available) {
                    RootHelperBridge.prepare();
                }
            }
            return makeDeviceInfo();
        }

        if ("device.screenState".equals(method)) {
            RootCommandResult rootResult = AndroidHostBridge.deviceScreenState();
            if (!rootResult.success) {
                throw new IllegalStateException(resolveRootCommandError(rootResult, "device screen state failed"));
            }
            return parseKeyValueObject(rootResult.stdout);
        }

        if ("device.wake".equals(method)) {
            RootCommandResult rootResult = AndroidHostBridge.deviceWake();
            if (!rootResult.success) {
                throw new IllegalStateException(resolveRootCommandError(rootResult, "device wake failed"));
            }

            JSONObject result = new JSONObject();
            result.put("ok", true);
            return result;
        }

        if ("device.sleep".equals(method)) {
            RootCommandResult rootResult = AndroidHostBridge.deviceSleep();
            if (!rootResult.success) {
                throw new IllegalStateException(resolveRootCommandError(rootResult, "device sleep failed"));
            }

            JSONObject result = new JSONObject();
            result.put("ok", true);
            return result;
        }

        if ("device.battery".equals(method)) {
            RootCommandResult rootResult = AndroidHostBridge.deviceBattery();
            if (!rootResult.success) {
                throw new IllegalStateException(resolveRootCommandError(rootResult, "device battery failed"));
            }
            return parseKeyValueObject(rootResult.stdout);
        }

        if ("device.rotation".equals(method)) {
            RootCommandResult rootResult = AndroidHostBridge.deviceRotation();
            if (!rootResult.success) {
                throw new IllegalStateException(resolveRootCommandError(rootResult, "device rotation failed"));
            }
            return parseKeyValueObject(rootResult.stdout);
        }

        if ("device.setRotation".equals(method)) {
            if (!params.has("rotation")) {
                throw new IllegalArgumentException("rotation is required");
            }

            RootCommandResult rootResult = AndroidHostBridge.deviceSetRotation(
                    params.optInt("rotation", 0),
                    params.optBoolean("locked", true)
            );
            if (!rootResult.success) {
                throw new IllegalStateException(resolveRootCommandError(rootResult, "device set rotation failed"));
            }

            JSONObject result = new JSONObject();
            result.put("ok", true);
            return result;
        }

        if ("device.settings.get".equals(method)) {
            RootCommandResult rootResult = AndroidHostBridge.deviceSettingsGet(
                    requireSettingsNamespace(params),
                    requireKey(params)
            );
            if (!rootResult.success) {
                throw new IllegalStateException(resolveRootCommandError(rootResult, "device settings get failed"));
            }

            JSONObject result = new JSONObject();
            result.put("value", rootResult.stdout.trim());
            return result;
        }

        if ("device.settings.put".equals(method)) {
            RootCommandResult rootResult = AndroidHostBridge.deviceSettingsPut(
                    requireSettingsNamespace(params),
                    requireKey(params),
                    requireValue(params)
            );
            if (!rootResult.success) {
                throw new IllegalStateException(resolveRootCommandError(rootResult, "device settings put failed"));
            }

            JSONObject result = new JSONObject();
            result.put("ok", true);
            return result;
        }

        if ("device.settings.delete".equals(method)) {
            RootCommandResult rootResult = AndroidHostBridge.deviceSettingsDelete(
                    requireSettingsNamespace(params),
                    requireKey(params)
            );
            if (!rootResult.success) {
                throw new IllegalStateException(resolveRootCommandError(rootResult, "device settings delete failed"));
            }

            JSONObject result = new JSONObject();
            result.put("ok", true);
            return result;
        }

        if ("device.prop.get".equals(method)) {
            RootCommandResult rootResult = AndroidHostBridge.devicePropGet(requireKey(params));
            if (!rootResult.success) {
                throw new IllegalStateException(resolveRootCommandError(rootResult, "device prop get failed"));
            }

            JSONObject result = new JSONObject();
            result.put("value", rootResult.stdout.trim());
            return result;
        }

        if ("device.prop.set".equals(method)) {
            RootCommandResult rootResult = AndroidHostBridge.devicePropSet(
                    requireKey(params),
                    requireValue(params)
            );
            if (!rootResult.success) {
                throw new IllegalStateException(resolveRootCommandError(rootResult, "device prop set failed"));
            }

            JSONObject result = new JSONObject();
            result.put("ok", true);
            return result;
        }

        if ("device.display.info".equals(method)) {
            RootCommandResult rootResult = AndroidHostBridge.deviceDisplayInfo();
            if (!rootResult.success) {
                throw new IllegalStateException(resolveRootCommandError(rootResult, "device display info failed"));
            }
            return parseKeyValueObject(rootResult.stdout);
        }

        if ("device.display.setSize".equals(method)) {
            RootCommandResult rootResult = AndroidHostBridge.deviceDisplaySetSize(
                    requirePositiveInt(params, "width"),
                    requirePositiveInt(params, "height")
            );
            if (!rootResult.success) {
                throw new IllegalStateException(resolveRootCommandError(rootResult, "device display set size failed"));
            }

            JSONObject result = new JSONObject();
            result.put("ok", true);
            return result;
        }

        if ("device.display.resetSize".equals(method)) {
            RootCommandResult rootResult = AndroidHostBridge.deviceDisplayResetSize();
            if (!rootResult.success) {
                throw new IllegalStateException(resolveRootCommandError(rootResult, "device display reset size failed"));
            }

            JSONObject result = new JSONObject();
            result.put("ok", true);
            return result;
        }

        if ("device.display.setDensity".equals(method)) {
            RootCommandResult rootResult = AndroidHostBridge.deviceDisplaySetDensity(
                    requirePositiveInt(params, "density")
            );
            if (!rootResult.success) {
                throw new IllegalStateException(resolveRootCommandError(rootResult, "device display set density failed"));
            }

            JSONObject result = new JSONObject();
            result.put("ok", true);
            return result;
        }

        if ("device.display.resetDensity".equals(method)) {
            RootCommandResult rootResult = AndroidHostBridge.deviceDisplayResetDensity();
            if (!rootResult.success) {
                throw new IllegalStateException(resolveRootCommandError(rootResult, "device display reset density failed"));
            }

            JSONObject result = new JSONObject();
            result.put("ok", true);
            return result;
        }

        if ("device.display.setBrightness".equals(method)) {
            RootCommandResult rootResult = AndroidHostBridge.deviceDisplaySetBrightness(
                    requireInt(params, "brightness")
            );
            if (!rootResult.success) {
                throw new IllegalStateException(resolveRootCommandError(rootResult, "device display set brightness failed"));
            }

            JSONObject result = new JSONObject();
            result.put("ok", true);
            return result;
        }

        if ("device.display.setAutoBrightness".equals(method)) {
            if (!params.has("enabled")) {
                throw new IllegalArgumentException("enabled is required");
            }

            RootCommandResult rootResult = AndroidHostBridge.deviceDisplaySetAutoBrightness(
                    requireBoolean(params, "enabled")
            );
            if (!rootResult.success) {
                throw new IllegalStateException(resolveRootCommandError(rootResult, "device display set auto brightness failed"));
            }

            JSONObject result = new JSONObject();
            result.put("ok", true);
            return result;
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

        if ("root.process.stats".equals(method)) {
            RootCommandResult rootResult = RootShellBridge.processStats(requireProcessTarget(params));
            if (!rootResult.success) {
                throw new IllegalStateException(resolveRootCommandError(rootResult, "root process stats failed"));
            }

            return parseProcessStats(rootResult.stdout);
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

        if ("app.disableComponent".equals(method)) {
            String componentName = requireComponentName(params);
            JSONObject result = new JSONObject();
            result.put("ok", AndroidHostBridge.appDisableComponent(componentName));
            return result;
        }

        if ("app.enableComponent".equals(method)) {
            String componentName = requireComponentName(params);
            JSONObject result = new JSONObject();
            result.put("ok", AndroidHostBridge.appEnableComponent(componentName));
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
            ensureRootModeCanRunScript();

            broadcastStatus(EngineService.STATE_RUNNING, "脚本运行中：IDE/HTTP");
            String message;
            try {
                message = NativeEngine.runLuaText(code);
            } catch (RuntimeException exception) {
                broadcastStatus(EngineService.STATE_FAILED, "脚本运行失败：" + exception.getMessage());
                throw exception;
            }
            JSONObject status = new JSONObject(NativeEngine.statusJson(0));
            String statusText = status.optString("status", "unknown");
            broadcastStatus(
                    "finished".equals(statusText) ? EngineService.STATE_FINISHED : EngineService.STATE_FAILED,
                    message
            );
            JSONObject result = new JSONObject();
            result.put("taskId", status.optInt("taskId", 0));
            result.put("message", message);
            result.put("status", statusText);
            return result;
        }

        if ("script.stop".equals(method)) {
            NativeEngine.stop();
            broadcastStatus(EngineService.STATE_STOPPING, "已请求停止脚本");
            JSONObject result = new JSONObject();
            result.put("accepted", true);
            return result;
        }

        if ("script.pause".equals(method)) {
            boolean accepted = NativeEngine.pause();
            broadcastStatus(
                    accepted ? EngineService.STATE_PAUSING : EngineService.STATE_FAILED,
                    accepted ? "已请求暂停脚本" : "当前没有可暂停的脚本"
            );
            JSONObject result = new JSONObject();
            result.put("accepted", accepted);
            result.put("status", new JSONObject(NativeEngine.statusJson(0)).optString("status", "unknown"));
            return result;
        }

        if ("script.resume".equals(method)) {
            boolean accepted = NativeEngine.resume();
            broadcastStatus(
                    accepted ? EngineService.STATE_RUNNING : EngineService.STATE_FAILED,
                    accepted ? "已请求继续脚本" : "当前没有已暂停的脚本"
            );
            JSONObject result = new JSONObject();
            result.put("accepted", accepted);
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

        if ("root.screen.capture".equals(method)) {
            JSONObject capture = new JSONObject(NativeEngine.captureRootScreenJson());
            if (!capture.optBoolean("ok", false)) {
                throw new IllegalStateException(capture.optString("error", "root screen capture failed"));
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

    private void ensureRootModeCanRunScript() {
        if (!EngineSettings.isRootModeEnabled(appContext)) {
            return;
        }

        // HTTP/IDE 运行脚本时不重新申请 Root。Root 模式的授权和常驻 shell
        // 初始化只在引擎启动或切换模式时做，避免 F5/运行按钮反复弹授权框。
        RootStatus rootStatus = RootShellBridge.status();
        if (!rootStatus.available || !RootShellBridge.isRootRuntimeReady()) {
            String error = rootStatus.error.isEmpty() ? "Root 权限不可用" : rootStatus.error;
            throw new IllegalStateException("Root 模式需要授权后才能运行脚本：" + error);
        }
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

    private void broadcastStatus(String state, String message) {
        Intent intent = new Intent(EngineService.ACTION_STATUS);
        intent.setPackage(appContext.getPackageName());
        intent.putExtra(EngineService.EXTRA_STATE, state);
        intent.putExtra(EngineService.EXTRA_MESSAGE, message == null ? "" : message);
        appContext.sendBroadcast(intent);
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

    private static JSONObject parseKeyValueObject(String text) throws JSONException {
        JSONObject result = new JSONObject();
        if (text == null || text.trim().isEmpty()) {
            return result;
        }

        String[] lines = text.split("\\r?\\n");
        for (String line : lines) {
            int separator = line.indexOf('=');
            if (separator <= 0) {
                continue;
            }

            String key = line.substring(0, separator).trim();
            String value = line.substring(separator + 1).trim();
            if (key.isEmpty()) {
                continue;
            }

            putTypedValue(result, key, value);
        }
        return result;
    }

    private static void putTypedValue(JSONObject result, String key, String value) throws JSONException {
        if ("true".equals(value) || "false".equals(value)) {
            result.put(key, Boolean.parseBoolean(value));
            return;
        }

        if (value.matches("-?\\d+")) {
            try {
                result.put(key, Long.parseLong(value));
                return;
            } catch (NumberFormatException ignored) {
                // 极端长数字保持字符串，避免接口整体失败。
            }
        }

        if (value.matches("-?\\d+\\.\\d+")) {
            try {
                result.put(key, Double.parseDouble(value));
                return;
            } catch (NumberFormatException ignored) {
                // 极端小数保持字符串。
            }
        }

        result.put(key, value);
    }

    private static String resolveRootCommandError(RootCommandResult rootResult, String defaultError) {
        if (!rootResult.error.isEmpty()) {
            return rootResult.error;
        }
        if (!rootResult.stderr.isEmpty()) {
            return rootResult.stderr;
        }
        return defaultError;
    }

    private static String requirePath(JSONObject params) {
        String path = params.optString("path", "");
        if (path.isEmpty()) {
            throw new IllegalArgumentException("path is required");
        }
        return path;
    }

    private static String requireSettingsNamespace(JSONObject params) {
        String namespace = params.optString("namespace", "");
        if (namespace.trim().isEmpty()) {
            throw new IllegalArgumentException("namespace is required");
        }
        return namespace;
    }

    private static String requireKey(JSONObject params) {
        String key = params.optString("key", "");
        if (key.trim().isEmpty()) {
            throw new IllegalArgumentException("key is required");
        }
        return key;
    }

    private static String requireValue(JSONObject params) {
        if (!params.has("value")) {
            throw new IllegalArgumentException("value is required");
        }
        return params.optString("value", "");
    }

    private static int requirePositiveInt(JSONObject params, String name) {
        int value = requireInt(params, name);
        if (value <= 0) {
            throw new IllegalArgumentException(name + " must be greater than 0");
        }
        return value;
    }

    private static int requireInt(JSONObject params, String name) {
        if (!params.has(name)) {
            throw new IllegalArgumentException(name + " is required");
        }
        try {
            return params.getInt(name);
        } catch (JSONException exception) {
            throw new IllegalArgumentException(name + " must be an integer");
        }
    }

    private static boolean requireBoolean(JSONObject params, String name) {
        if (!params.has(name)) {
            throw new IllegalArgumentException(name + " is required");
        }
        try {
            return params.getBoolean(name);
        } catch (JSONException exception) {
            throw new IllegalArgumentException(name + " must be a boolean");
        }
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

    private static JSONObject parseProcessStats(String stdout) throws JSONException {
        JSONObject result = new JSONObject();
        if (stdout == null || stdout.trim().isEmpty()) {
            throw new IllegalStateException("root process stats output is empty");
        }

        // /proc/<pid>/status 是 key:value 文本。这里只挑脚本层常用字段，
        // 字段缺失时保持默认不写入，避免不同 Android 内核版本的差异放大成失败。
        String[] lines = stdout.split("\\r?\\n");
        for (String line : lines) {
            int separator = line.indexOf(':');
            if (separator <= 0) {
                continue;
            }

            String key = line.substring(0, separator).trim();
            String value = line.substring(separator + 1).trim();
            if ("Name".equals(key)) {
                result.put("name", value);
            } else if ("State".equals(key)) {
                result.put("state", value);
            } else if ("Pid".equals(key)) {
                result.put("pid", parseLeadingLong(value, 0));
            } else if ("PPid".equals(key)) {
                result.put("ppid", parseLeadingLong(value, 0));
            } else if ("TracerPid".equals(key)) {
                result.put("tracerPid", parseLeadingLong(value, 0));
            } else if ("Uid".equals(key)) {
                result.put("uid", parseLeadingLong(value, -1));
            } else if ("Gid".equals(key)) {
                result.put("gid", parseLeadingLong(value, -1));
            } else if ("Threads".equals(key)) {
                result.put("threads", parseLeadingLong(value, 0));
            } else if ("VmPeak".equals(key)) {
                result.put("vmPeakKb", parseLeadingLong(value, 0));
            } else if ("VmSize".equals(key)) {
                result.put("vmSizeKb", parseLeadingLong(value, 0));
            } else if ("VmRSS".equals(key)) {
                result.put("vmRssKb", parseLeadingLong(value, 0));
            } else if ("RssAnon".equals(key)) {
                result.put("rssAnonKb", parseLeadingLong(value, 0));
            } else if ("RssFile".equals(key)) {
                result.put("rssFileKb", parseLeadingLong(value, 0));
            } else if ("RssShmem".equals(key)) {
                result.put("rssShmemKb", parseLeadingLong(value, 0));
            } else if ("VmData".equals(key)) {
                result.put("vmDataKb", parseLeadingLong(value, 0));
            } else if ("VmStk".equals(key)) {
                result.put("vmStackKb", parseLeadingLong(value, 0));
            } else if ("VmExe".equals(key)) {
                result.put("vmExeKb", parseLeadingLong(value, 0));
            } else if ("VmLib".equals(key)) {
                result.put("vmLibKb", parseLeadingLong(value, 0));
            } else if ("voluntary_ctxt_switches".equals(key)) {
                result.put("voluntaryContextSwitches", parseLeadingLong(value, 0));
            } else if ("nonvoluntary_ctxt_switches".equals(key)) {
                result.put("nonvoluntaryContextSwitches", parseLeadingLong(value, 0));
            }
        }

        if (!result.has("pid") && !result.has("name")) {
            throw new IllegalStateException("root process stats output is invalid");
        }
        return result;
    }

    private static long parseLeadingLong(String value, long defaultValue) {
        if (value == null) {
            return defaultValue;
        }

        String trimmed = value.trim();
        if (trimmed.isEmpty()) {
            return defaultValue;
        }

        int end = 0;
        while (end < trimmed.length()) {
            char ch = trimmed.charAt(end);
            if ((ch >= '0' && ch <= '9') || (end == 0 && ch == '-')) {
                end++;
                continue;
            }
            break;
        }
        if (end == 0 || (end == 1 && trimmed.charAt(0) == '-')) {
            return defaultValue;
        }

        try {
            return Long.parseLong(trimmed.substring(0, end));
        } catch (NumberFormatException ignored) {
            return defaultValue;
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

    private static String requireComponentName(JSONObject params) {
        String componentName = params.optString("component", "");
        if (componentName.trim().isEmpty()) {
            componentName = params.optString("componentName", "");
        }
        if (componentName.trim().isEmpty()) {
            throw new IllegalArgumentException("component is required");
        }
        return componentName;
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
