/**
 * 文件用途：RootDaemon 启动的一次性 uid=0 脚本 Worker 入口。
 */
package com.xiaoyv.engine;

import android.content.Context;
import android.net.Uri;
import android.os.Bundle;
import android.os.Looper;
import android.util.Log;

import java.lang.reflect.Method;
import java.io.File;
import java.util.concurrent.CountDownLatch;

/** Root 外壳只负责取得包 Context 和注册 Binder，运行时内核与 Local Worker 共用。 */
public final class EngineWorkerMain {
    private static final String TAG = "小鱼精灵";
    private EngineWorkerMain() {
    }

    public static void main(String[] args) {
        Arguments arguments = Arguments.parse(args);
        if (arguments == null) {
            return;
        }

        EngineWorkerEndpoint endpoint = null;
        try {
            if (Looper.myLooper() == null) {
                Looper.prepareMainLooper();
            }
            Context context = createPackageContext(arguments.packageName);
            prepareRootJavaRuntime();
            CountDownLatch shutdown = new CountDownLatch(1);
            endpoint = new EngineWorkerEndpoint(
                    context,
                    arguments.nativeLibraryDirectory,
                    shutdown::countDown
            );

            Bundle extras = new Bundle();
            extras.putString(EngineWorkerBridgeProvider.EXTRA_TOKEN, arguments.token);
            extras.putBinder(EngineWorkerBridgeProvider.EXTRA_BINDER, endpoint.asBinder());
            Bundle result = ContentProviderBridge.call(
                    context,
                    Uri.parse("content://" + arguments.authority),
                    EngineWorkerBridgeProvider.METHOD_REGISTER,
                    arguments.runId,
                    extras
            );
            if (result == null
                    || !result.getBoolean(EngineWorkerBridgeProvider.RESULT_ACCEPTED, false)) {
                endpoint.close();
                return;
            }
            shutdown.await();
        } catch (Throwable error) {
            // Root app_process 没有 Activity 的崩溃界面，保留一条 logcat 根因供启动失败定位。
            Log.e(TAG, "Root Worker 启动失败", error);
        } finally {
            if (endpoint != null) {
                endpoint.close();
            }
            System.exit(0);
        }
    }

    private static Context createPackageContext(String packageName) throws Exception {
        Class<?> activityThreadClass = Class.forName("android.app.ActivityThread");
        Method systemMain = activityThreadClass.getDeclaredMethod("systemMain");
        systemMain.setAccessible(true);
        Object activityThread = systemMain.invoke(null);
        Method getSystemContext = activityThreadClass.getDeclaredMethod("getSystemContext");
        getSystemContext.setAccessible(true);
        Context systemContext = (Context) getSystemContext.invoke(activityThread);
        return systemContext.createPackageContext(
                packageName,
                Context.CONTEXT_INCLUDE_CODE | Context.CONTEXT_IGNORE_SECURITY
        );
    }

    private static void prepareRootJavaRuntime() {
        File javaCrypto = new File("/system/lib64/libjavacrypto.so");
        if (!javaCrypto.isFile()) {
            javaCrypto = new File("/system/lib/libjavacrypto.so");
        }
        if (!javaCrypto.isFile()) return;
        try {
            System.load(javaCrypto.getAbsolutePath());
        } catch (UnsatisfiedLinkError error) {
            // 部分 Android 版本已由 RuntimeInit 注册 Conscrypt native；重复加载无需失败。
            Log.d(TAG, "Root Java 加密运行库已由系统提供：" + error.getMessage());
        }
    }

    private static final class Arguments {
        private String packageName;
        private String runId;
        private String token;
        private String authority;
        private String nativeLibraryDirectory;

        private static Arguments parse(String[] args) {
            Arguments result = new Arguments();
            for (int index = 0; args != null && index < args.length; index++) {
                String key = args[index];
                if (index + 1 >= args.length) {
                    return null;
                }
                String value = args[++index];
                if ("--package".equals(key)) result.packageName = value;
                else if ("--run-id".equals(key)) result.runId = value;
                else if ("--token".equals(key)) result.token = value;
                else if ("--authority".equals(key)) result.authority = value;
                else if ("--native-lib-dir".equals(key)) result.nativeLibraryDirectory = value;
                else return null;
            }
            if (empty(result.packageName)
                    || empty(result.runId)
                    || !RootDaemonClient.isValidToken(result.token)
                    || empty(result.authority)
                    || empty(result.nativeLibraryDirectory)) {
                return null;
            }
            return result;
        }

        private static boolean empty(String value) {
            return value == null || value.trim().isEmpty();
        }
    }
}
