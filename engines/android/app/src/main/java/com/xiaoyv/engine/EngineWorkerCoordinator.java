/**
 * 文件用途：常驻 :engine 控制进程管理一次性 Root/非 Root 脚本 Worker。
 */
package com.xiaoyv.engine;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.IBinder;
import android.os.Build;
import android.os.ParcelFileDescriptor;
import android.os.RemoteException;
import android.util.Base64;
import android.view.Surface;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/**
 * 运行时只有一份：RootDaemon 与 Android Service 只是两种权限不同的启动外壳。
 * 每次脚本结束都会关闭 Worker，下一次运行取得全新的 Java/native/语言运行时进程。
 */
final class EngineWorkerCoordinator {
    private static final Object LOCK = new Object();
    private static final Object START_LOCK = new Object();
    private static final Object LOG_LOCK = new Object();
    private static final long START_TIMEOUT_MS = 8000L;
    private static final int MAX_PIPE_BYTES = 64 * 1024 * 1024;
    private static final int MAX_RETAINED_LOGS = 1000;
    private static final SecureRandom RANDOM = new SecureRandom();

    private static Context appContext;
    private static IEngineWorker worker;
    private static IBinder workerBinder;
    private static IBinder.DeathRecipient deathRecipient;
    private static String workerRunId;
    private static boolean workerRoot;
    private static ServiceConnection localConnection;

    private static String pendingRootRunId;
    private static String pendingRootToken;
    private static CountDownLatch pendingRootReady;
    private static CountDownLatch pendingLocalReady;

    private static int workerNativeLogAfterId;
    private static int nextLogId = 1;
    private static final List<LogRecord> retainedLogs = new ArrayList<>();

    private EngineWorkerCoordinator() {
    }

    static void initialize(Context context) {
        if (context == null) {
            return;
        }
        synchronized (LOCK) {
            Context application = context.getApplicationContext();
            appContext = application == null ? context : application;
        }
    }

    static boolean registerRootWorker(String runId, String token, IEngineWorker candidate) {
        synchronized (LOCK) {
            if (candidate == null
                    || runId == null
                    || token == null
                    || !runId.equals(pendingRootRunId)
                    || !constantTimeEquals(token, pendingRootToken)) {
                return false;
            }
            try {
                if (candidate.processUid() != 0) {
                    return false;
                }
                installWorkerLocked(candidate, runId, true);
                if (pendingRootReady != null) {
                    pendingRootReady.countDown();
                }
                return true;
            } catch (RemoteException exception) {
                return false;
            }
        }
    }

    static String callJson(Context context, String method, String paramsJson) {
        initialize(context);
        String safeMethod = method == null ? "" : method;
        String safeParams = paramsJson == null || paramsJson.trim().isEmpty()
                ? "{}"
                : paramsJson;

        if ("log.drain".equals(safeMethod)) {
            return drainLogs(safeParams);
        }
        if ("device.setRootModeEnabled".equals(safeMethod)) {
            return changeRootMode(safeParams);
        }

        IEngineWorker current = currentWorker();
        if (current == null) {
            String idle = idleResponse(safeMethod);
            if (idle != null) {
                return idle;
            }
        }

        boolean closesSession = "script.run".equals(safeMethod)
                || "script.runPackage".equals(safeMethod);
        IEngineWorker target = current == null ? ensureWorker() : current;
        try {
            return invokeJson(target, safeMethod, safeParams);
        } finally {
            if (closesSession) {
                harvestLogs(target);
                shutdownCurrent(false);
            }
        }
    }

    static byte[] getScreenFrame(Context context) {
        initialize(context);
        IEngineWorker target = ensureWorker();
        try {
            ParcelFileDescriptor descriptor = target.openScreenFrame();
            return readBytes(descriptor, MAX_PIPE_BYTES);
        } catch (RemoteException exception) {
            throw workerFailure("读取截图失败", exception);
        }
    }

    /**
     * 同步 App 主进程设置页已经确认的运行模式。
     *
     * SharedPreferences 不提供可靠的多进程缓存同步，因此必须在 :engine 内再写一次，并关闭
     * 可能按旧 UID 创建的空闲 Worker，确保下一次运行按新模式重新创建。
     */
    static void syncRootMode(Context context, boolean enabled) {
        initialize(context);
        Context controllerContext;
        synchronized (LOCK) {
            controllerContext = appContext;
        }
        if (controllerContext == null) {
            return;
        }
        EngineSettings.setRootModeEnabled(controllerContext, enabled);
        shutdownCurrent(false);
    }

    static void forceStop() {
        shutdownCurrent(true);
    }

    static void shutdown() {
        shutdownCurrent(true);
    }

    static boolean attachImGuiSurface(Surface surface) {
        IEngineWorker target = currentWorker();
        if (target == null) return false;
        try {
            return target.attachImGuiSurface(surface);
        } catch (RemoteException exception) {
            return false;
        }
    }

    static void detachImGuiSurface() {
        IEngineWorker target = currentWorker();
        if (target == null) return;
        try {
            target.detachImGuiSurface();
        } catch (RemoteException ignored) {
            // Worker 已退出时 Surface 也会由服务侧回收。
        }
    }

    static void notifyImGuiSurfaceFailure(String message) {
        IEngineWorker target = currentWorker();
        if (target == null) return;
        try {
            target.notifyImGuiSurfaceFailure(message);
        } catch (RemoteException ignored) {
            // 同上。
        }
    }

    static void enqueueImGuiTouch(int action, int pointerId, float x, float y) {
        IEngineWorker target = currentWorker();
        if (target == null) return;
        try {
            target.enqueueImGuiTouch(action, pointerId, x, y);
        } catch (RemoteException ignored) {
            // 输入到达时 Worker 可能刚好结束。
        }
    }

    static void enqueueImGuiText(String text) {
        IEngineWorker target = currentWorker();
        if (target == null) return;
        try {
            target.enqueueImGuiText(text);
        } catch (RemoteException ignored) {
            // 输入到达时 Worker 可能刚好结束。
        }
    }

    static void enqueueImGuiKey(int action, int keyCode, int unicodeCodePoint, int metaState) {
        IEngineWorker target = currentWorker();
        if (target == null) return;
        try {
            target.enqueueImGuiKey(action, keyCode, unicodeCodePoint, metaState);
        } catch (RemoteException ignored) {
            // 输入到达时 Worker 可能刚好结束。
        }
    }

    static void enqueueImGuiScroll(float horizontal, float vertical) {
        IEngineWorker target = currentWorker();
        if (target == null) return;
        try {
            target.enqueueImGuiScroll(horizontal, vertical);
        } catch (RemoteException ignored) {
            // 输入到达时 Worker 可能刚好结束。
        }
    }

    private static IEngineWorker ensureWorker() {
        Context context;
        synchronized (LOCK) {
            context = appContext;
        }
        if (context == null) {
            throw new IllegalStateException("引擎控制进程尚未初始化");
        }

        boolean root = EngineSettings.isRootModeEnabled(context);
        synchronized (START_LOCK) {
            IEngineWorker existing = currentWorker();
            if (existing != null) {
                synchronized (LOCK) {
                    if (workerRoot == root) {
                        return existing;
                    }
                }
                shutdownCurrent(false);
            }
            return root ? startRootWorker(context) : startLocalWorker(context);
        }
    }

    private static IEngineWorker startRootWorker(Context context) {
        if (!RootDaemonClient.isReady(context)) {
            throw new IllegalStateException("Root 模式已开启，但 RootDaemon 未就绪");
        }
        verifyRootWorkerAbi(context);

        String runId = newRunId();
        String token = newToken();
        CountDownLatch ready = new CountDownLatch(1);
        synchronized (LOCK) {
            pendingRootRunId = runId;
            pendingRootToken = token;
            pendingRootReady = ready;
            workerRunId = runId;
            workerRoot = true;
        }

        try {
            RootDaemonClient.startWorker(
                    context,
                    runId,
                    token,
                    context.getPackageName() + ".engineworker"
            );
            if (!ready.await(START_TIMEOUT_MS, TimeUnit.MILLISECONDS)) {
                throw new IllegalStateException("Root Worker 未在限定时间内连接控制进程");
            }
            IEngineWorker result = currentWorker();
            if (result == null) {
                throw new IllegalStateException("Root Worker Binder 不可用");
            }
            return result;
        } catch (IOException exception) {
            throw workerFailure("启动 Root Worker 失败", exception);
        } catch (InterruptedException exception) {
            Thread.currentThread().interrupt();
            throw workerFailure("等待 Root Worker 时被中断", exception);
        } finally {
            synchronized (LOCK) {
                if (runId.equals(pendingRootRunId)) {
                    pendingRootRunId = null;
                    pendingRootToken = null;
                    pendingRootReady = null;
                }
                if (worker == null && runId.equals(workerRunId)) {
                    workerRunId = null;
                }
            }
            if (currentWorker() == null) {
                RootDaemonClient.stopWorker(context, runId);
            }
        }
    }

    private static IEngineWorker startLocalWorker(Context context) {
        CountDownLatch ready = new CountDownLatch(1);
        String runId = newRunId();
        ServiceConnection connection = new ServiceConnection() {
            @Override
            public void onServiceConnected(ComponentName name, IBinder service) {
                IEngineWorker candidate = IEngineWorker.Stub.asInterface(service);
                synchronized (LOCK) {
                    if (localConnection != this || candidate == null) {
                        return;
                    }
                    installWorkerLocked(candidate, runId, false);
                    ready.countDown();
                }
            }

            @Override
            public void onServiceDisconnected(ComponentName name) {
                handleLocalDisconnect(this);
            }

            @Override
            public void onBindingDied(ComponentName name) {
                handleLocalDisconnect(this);
            }
        };

        synchronized (LOCK) {
            workerRunId = runId;
            workerRoot = false;
            localConnection = connection;
            pendingLocalReady = ready;
        }
        Intent intent = new Intent(context, LocalEngineWorkerService.class);
        if (!context.bindService(intent, connection, Context.BIND_AUTO_CREATE | Context.BIND_IMPORTANT)) {
            synchronized (LOCK) {
                if (localConnection == connection) {
                    localConnection = null;
                    workerRunId = null;
                    pendingLocalReady = null;
                }
            }
            throw new IllegalStateException("启动非 Root Worker 失败");
        }

        try {
            if (!ready.await(START_TIMEOUT_MS, TimeUnit.MILLISECONDS)) {
                throw new IllegalStateException("非 Root Worker 未在限定时间内连接控制进程");
            }
            IEngineWorker result = currentWorker();
            if (result == null) {
                throw new IllegalStateException("非 Root Worker Binder 不可用");
            }
            return result;
        } catch (InterruptedException exception) {
            Thread.currentThread().interrupt();
            throw workerFailure("等待非 Root Worker 时被中断", exception);
        } catch (RuntimeException exception) {
            shutdownCurrent(true);
            throw exception;
        } finally {
            synchronized (LOCK) {
                if (pendingLocalReady == ready) {
                    pendingLocalReady = null;
                }
            }
        }
    }

    private static void installWorkerLocked(
            IEngineWorker candidate,
            String runId,
            boolean root
    ) {
        IBinder binder = candidate.asBinder();
        IBinder.DeathRecipient recipient = () -> handleBinderDeath(binder);
        try {
            binder.linkToDeath(recipient, 0);
        } catch (RemoteException exception) {
            throw new IllegalStateException("Worker 在连接时已经退出", exception);
        }
        worker = candidate;
        workerBinder = binder;
        deathRecipient = recipient;
        workerRunId = runId;
        workerRoot = root;
        workerNativeLogAfterId = 0;
    }

    private static void handleBinderDeath(IBinder expected) {
        boolean removed = false;
        synchronized (LOCK) {
            if (workerBinder != null && (expected == null || workerBinder == expected)) {
                clearWorkerLocked();
                removed = true;
            }
            if (pendingRootReady != null) pendingRootReady.countDown();
            if (pendingLocalReady != null) pendingLocalReady.countDown();
        }
        if (removed) {
            EngineUiHost.closeAll(appContext);
        }
    }

    private static void handleLocalDisconnect(ServiceConnection source) {
        IBinder expected;
        synchronized (LOCK) {
            if (localConnection != source) {
                return;
            }
            expected = workerBinder;
            localConnection = null;
        }
        handleBinderDeath(expected);
    }

    private static IEngineWorker currentWorker() {
        synchronized (LOCK) {
            if (workerBinder == null || !workerBinder.isBinderAlive()) {
                if (workerBinder != null) {
                    clearWorkerLocked();
                }
                return null;
            }
            return worker;
        }
    }

    private static void shutdownCurrent(boolean force) {
        Context context;
        IEngineWorker target;
        ServiceConnection connection;
        String runId;
        boolean root;
        int pid = -1;
        synchronized (LOCK) {
            context = appContext;
            target = worker;
            connection = localConnection;
            runId = workerRunId;
            root = workerRoot;
            if (target != null && force) {
                try {
                    pid = target.processId();
                } catch (RemoteException ignored) {
                    // 已死亡的 Worker 直接按其启动外壳清理。
                }
            }
            clearWorkerLocked();
            localConnection = null;
            pendingRootRunId = null;
            pendingRootToken = null;
            if (pendingRootReady != null) pendingRootReady.countDown();
            pendingRootReady = null;
            if (pendingLocalReady != null) pendingLocalReady.countDown();
            pendingLocalReady = null;
        }

        EngineUiHost.closeAll(context);
        if (target != null && !force) {
            try {
                target.shutdown();
            } catch (RemoteException ignored) {
                // 由下面的进程外壳负责最终回收。
            }
        }
        if (connection != null && context != null) {
            try {
                context.unbindService(connection);
            } catch (IllegalArgumentException ignored) {
                // 绑定已由系统解除。
            }
        }
        if (root && context != null && runId != null && (force || target == null)) {
            RootDaemonClient.stopWorker(context, runId);
        } else if (!root && force && pid > 0) {
            android.os.Process.killProcess(pid);
        } else if (!root && target != null && force) {
            try {
                target.shutdown();
            } catch (RemoteException ignored) {
                // 本地进程已退出。
            }
        }
        if (target != null
                && context != null
                && root != EngineSettings.isRootModeEnabled(context)) {
            RootDaemonService.setRootModeEnabled(
                    context,
                    EngineSettings.isRootModeEnabled(context)
            );
        }
    }

    private static String changeRootMode(String paramsJson) {
        Context context;
        synchronized (LOCK) {
            context = appContext;
        }
        if (context == null) {
            throw new IllegalStateException("引擎控制进程尚未初始化");
        }
        boolean enabled;
        try {
            JSONObject params = new JSONObject(paramsJson);
            if (!params.has("enabled") || !(params.opt("enabled") instanceof Boolean)) {
                throw new IllegalArgumentException("enabled 参数必须是布尔值");
            }
            enabled = params.getBoolean("enabled");
        } catch (JSONException exception) {
            throw new IllegalArgumentException("Root 模式参数无效", exception);
        }

        IEngineWorker target;
        try {
            target = ensureWorker();
        } catch (RuntimeException startError) {
            if (enabled) throw startError;
            EngineSettings.setRootModeEnabled(context, false);
            RootDaemonService.setRootModeEnabled(context, false);
            target = ensureWorker();
        }

        EngineSettings.setRootModeEnabled(context, enabled);
        String response;
        try {
            response = invokeJson(target, "device.info", "{}");
        } finally {
            shutdownCurrent(false);
        }
        return patchRootModeResponse(response, enabled, context);
    }

    private static String patchRootModeResponse(
            String response,
            boolean enabled,
            Context context
    ) {
        try {
            JSONObject envelope = new JSONObject(response);
            JSONObject result = envelope.optJSONObject("result");
            if (!envelope.optBoolean("ok", false) || result == null) return response;
            boolean ready = enabled && RootDaemonClient.isReady(context);
            boolean accessibility = AndroidHostBridge.isAccessibilityEnabled();
            result.put("rootModeEnabled", enabled);
            result.put("rootAvailable", ready);
            result.put("rootRuntimeReady", ready);
            result.put(
                    "automationMode",
                    ready ? "root" : accessibility ? "accessibility" : "none"
            );
            JSONObject rootStatus = new JSONObject();
            rootStatus.put("available", ready);
            rootStatus.put("commandMode", "ROOT_DAEMON");
            rootStatus.put("suPath", "su");
            rootStatus.put("cached", false);
            rootStatus.put("cacheExpireAt", 0);
            rootStatus.put(
                    "error",
                    ready ? "" : enabled ? "RootDaemon 正在准备" : "Root 模式未开启"
            );
            rootStatus.put("attempts", new JSONArray());
            result.put("rootStatus", rootStatus);
            return envelope.toString();
        } catch (JSONException exception) {
            return response;
        }
    }

    private static void clearWorkerLocked() {
        if (workerBinder != null && deathRecipient != null) {
            workerBinder.unlinkToDeath(deathRecipient, 0);
        }
        worker = null;
        workerBinder = null;
        deathRecipient = null;
        workerRunId = null;
        workerNativeLogAfterId = 0;
    }

    private static String invokeJson(IEngineWorker target, String method, String paramsJson) {
        ParcelFileDescriptor[] requestPipe = null;
        try {
            requestPipe = ParcelFileDescriptor.createPipe();
            ParcelFileDescriptor requestRead = requestPipe[0];
            ParcelFileDescriptor requestWrite = requestPipe[1];
            byte[] params = paramsJson.getBytes(StandardCharsets.UTF_8);
            Thread writer = new Thread(
                    () -> writeRequest(requestWrite, params),
                    "EngineWorkerJsonRequest"
            );
            writer.setDaemon(true);
            writer.start();
            ParcelFileDescriptor response;
            try (ParcelFileDescriptor ignored = requestRead) {
                response = target.callJsonPipe(method, requestRead);
            }
            return new String(readBytes(response, MAX_PIPE_BYTES), StandardCharsets.UTF_8);
        } catch (IOException | RemoteException exception) {
            if (requestPipe != null) {
                closeQuietly(requestPipe[0]);
                closeQuietly(requestPipe[1]);
            }
            throw workerFailure("调用脚本 Worker 失败", exception);
        }
    }

    private static void writeRequest(ParcelFileDescriptor descriptor, byte[] bytes) {
        try (OutputStream output = new ParcelFileDescriptor.AutoCloseOutputStream(descriptor)) {
            output.write(bytes);
        } catch (IOException ignored) {
            // Worker 退出后请求自然作废。
        }
    }

    private static byte[] readBytes(ParcelFileDescriptor descriptor, int maxBytes) {
        if (descriptor == null) {
            throw new IllegalStateException("Worker 未返回数据管道");
        }
        try (InputStream input = new ParcelFileDescriptor.AutoCloseInputStream(descriptor);
             ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[8192];
            int count;
            while ((count = input.read(buffer)) != -1) {
                if (output.size() + count > maxBytes) {
                    throw new IllegalStateException("Worker 返回数据过大");
                }
                output.write(buffer, 0, count);
            }
            return output.toByteArray();
        } catch (IOException exception) {
            throw workerFailure("读取 Worker 返回数据失败", exception);
        }
    }

    private static void harvestLogs(IEngineWorker target) {
        if (target == null) return;
        synchronized (LOG_LOCK) {
            int after;
            synchronized (LOCK) {
                after = workerNativeLogAfterId;
            }
            try {
                JSONObject envelope = new JSONObject(invokeJson(
                        target,
                        "log.drain",
                        new JSONObject().put("afterId", after).toString()
                ));
                JSONObject result = envelope.optJSONObject("result");
                if (!envelope.optBoolean("ok", false) || result == null) return;
                JSONArray entries = result.optJSONArray("entries");
                if (entries == null) return;
                synchronized (LOCK) {
                    for (int index = 0; index < entries.length(); index++) {
                        JSONObject entry = entries.optJSONObject(index);
                        if (entry == null) continue;
                        int nativeId = entry.optInt("id", after);
                        after = Math.max(after, nativeId);
                        retainedLogs.add(new LogRecord(
                                nextLogId++,
                                entry.optString("level", "info"),
                                entry.optString("message", "")
                        ));
                    }
                    workerNativeLogAfterId = after;
                    while (retainedLogs.size() > MAX_RETAINED_LOGS) {
                        retainedLogs.remove(0);
                    }
                }
            } catch (RuntimeException | JSONException ignored) {
                // 日志回收失败不改变脚本主结果。
            }
        }
    }

    private static String drainLogs(String paramsJson) {
        IEngineWorker target = currentWorker();
        if (target != null) {
            harvestLogs(target);
        }
        int afterId = 0;
        try {
            afterId = new JSONObject(paramsJson).optInt("afterId", 0);
        } catch (JSONException ignored) {
            // 与 native 一致，缺失游标按 0 处理。
        }
        try {
            JSONArray entries = new JSONArray();
            int lastId = afterId;
            synchronized (LOCK) {
                for (LogRecord record : retainedLogs) {
                    if (record.id <= afterId) continue;
                    entries.put(record.toJson());
                    lastId = record.id;
                }
            }
            JSONObject result = new JSONObject();
            result.put("entries", entries);
            result.put("lastId", lastId);
            return successEnvelope(result).toString();
        } catch (JSONException exception) {
            throw new IllegalStateException("生成日志结果失败", exception);
        }
    }

    private static String idleResponse(String method) {
        try {
            if ("script.stop".equals(method)
                    || "script.pause".equals(method)
                    || "script.resume".equals(method)) {
                return successEnvelope(new JSONObject()
                        .put("accepted", false)
                        .put("status", "idle")).toString();
            }
            if ("script.status".equals(method)) {
                return successEnvelope(new JSONObject()
                        .put("taskId", 0)
                        .put("message", "")
                        .put("status", "idle")).toString();
            }
            if ("ui.closeAll".equals(method)) {
                return successEnvelope(new JSONObject().put("closed", true)).toString();
            }
            if ("ui.event".equals(method)) {
                return errorEnvelope("当前没有运行中的脚本 Worker").toString();
            }
            return null;
        } catch (JSONException exception) {
            throw new IllegalStateException("生成空闲状态失败", exception);
        }
    }

    private static JSONObject successEnvelope(Object result) throws JSONException {
        return new JSONObject().put("ok", true).put("result", result);
    }

    private static JSONObject errorEnvelope(String message) throws JSONException {
        return new JSONObject().put("ok", false).put("code", -32000).put("error", message);
    }

    private static String newRunId() {
        return UUID.randomUUID().toString().replace("-", "");
    }

    private static void verifyRootWorkerAbi(Context context) {
        String nativeDirectory = context.getApplicationInfo().nativeLibraryDir;
        String apkAbi = nativeDirectory != null && nativeDirectory.contains("x86_64")
                ? "x86_64"
                : nativeDirectory != null && nativeDirectory.contains("arm64")
                ? "arm64-v8a"
                : "unknown";
        String systemAbi = Build.SUPPORTED_ABIS == null || Build.SUPPORTED_ABIS.length == 0
                ? "unknown"
                : Build.SUPPORTED_ABIS[0];
        if (!"unknown".equals(apkAbi) && !apkAbi.equals(systemAbi)) {
            throw new IllegalStateException(
                    "Root Worker ABI 不匹配：当前 APK=" + apkAbi
                            + "，系统 app_process=" + systemAbi
                            + "；请安装对应 ABI 的 APK"
            );
        }
    }

    private static String newToken() {
        byte[] bytes = new byte[32];
        RANDOM.nextBytes(bytes);
        return Base64.encodeToString(
                bytes,
                Base64.URL_SAFE | Base64.NO_PADDING | Base64.NO_WRAP
        );
    }

    private static boolean constantTimeEquals(String left, String right) {
        if (left == null || right == null) return false;
        byte[] leftBytes = left.getBytes(StandardCharsets.UTF_8);
        byte[] rightBytes = right.getBytes(StandardCharsets.UTF_8);
        return java.security.MessageDigest.isEqual(leftBytes, rightBytes);
    }

    private static IllegalStateException workerFailure(String prefix, Exception exception) {
        String message = exception.getMessage();
        return new IllegalStateException(
                message == null || message.isEmpty() ? prefix : prefix + "：" + message,
                exception
        );
    }

    private static void closeQuietly(ParcelFileDescriptor descriptor) {
        if (descriptor == null) return;
        try {
            descriptor.close();
        } catch (IOException ignored) {
            // 失败路径只负责尽力释放管道。
        }
    }

    private static final class LogRecord {
        private final int id;
        private final String level;
        private final String message;

        private LogRecord(int id, String level, String message) {
            this.id = id;
            this.level = level;
            this.message = message;
        }

        private JSONObject toJson() throws JSONException {
            return new JSONObject()
                    .put("id", id)
                    .put("level", level)
                    .put("message", message);
        }
    }
}
