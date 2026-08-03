/**
 * 文件用途：让普通 App Worker 与未注册到 AMS 的 root app_process 统一调用本包 Provider。
 */
package com.xiaoyv.engine;

import android.content.Context;
import android.net.Uri;
import android.os.Binder;
import android.os.Bundle;
import android.os.IBinder;

import java.lang.reflect.Field;
import java.lang.reflect.Method;

/**
 * 普通进程走公开 ContentResolver；root app_process 没有 ProcessRecord，必须按 Android shell
 * 同类方式临时取得 external provider Binder，否则 AMS 会拒绝“找不到 caller app”。
 */
final class ContentProviderBridge {
    private ContentProviderBridge() {
    }

    static Bundle call(Context context, Uri uri, String method, String arg, Bundle extras) {
        if (context == null || uri == null || uri.getAuthority() == null) {
            throw new IllegalArgumentException("Provider 调用参数无效");
        }
        if (android.os.Process.myUid() != 0) {
            return context.getContentResolver().call(uri, method, arg, extras);
        }
        return callAsExternal(context, uri.getAuthority(), method, arg, extras);
    }

    private static Bundle callAsExternal(
            Context context,
            String authority,
            String method,
            String arg,
            Bundle extras
    ) {
        IBinder token = new Binder();
        Object activityManager = null;
        try {
            Class<?> activityManagerClass = Class.forName("android.app.ActivityManager");
            Method getService = activityManagerClass.getDeclaredMethod("getService");
            getService.setAccessible(true);
            activityManager = getService.invoke(null);
            Object holder = acquireExternalProvider(activityManager, authority, token);
            if (holder == null) {
                throw new IllegalStateException("无法取得 Provider：" + authority);
            }
            Field providerField = holder.getClass().getDeclaredField("provider");
            providerField.setAccessible(true);
            Object provider = providerField.get(holder);
            if (provider == null) {
                throw new IllegalStateException("Provider Binder 不可用：" + authority);
            }
            return invokeProviderCall(provider, context, authority, method, arg, extras);
        } catch (ReflectiveOperationException exception) {
            throw new IllegalStateException("Root Provider 桥调用失败", exception);
        } finally {
            releaseExternalProvider(activityManager, authority, token);
        }
    }

    private static Object acquireExternalProvider(
            Object activityManager,
            String authority,
            IBinder token
    ) throws ReflectiveOperationException {
        for (Method candidate : activityManager.getClass().getMethods()) {
            if (!candidate.getName().startsWith("getContentProviderExternal")) continue;
            candidate.setAccessible(true);
            return candidate.invoke(
                    activityManager,
                    externalProviderArguments(candidate.getParameterTypes(), authority, token)
            );
        }
        throw new NoSuchMethodException("getContentProviderExternal");
    }

    private static void releaseExternalProvider(
            Object activityManager,
            String authority,
            IBinder token
    ) {
        if (activityManager == null) return;
        try {
            for (Method candidate : activityManager.getClass().getMethods()) {
                if (!candidate.getName().startsWith("removeContentProviderExternal")) continue;
                candidate.setAccessible(true);
                candidate.invoke(
                        activityManager,
                        externalProviderArguments(candidate.getParameterTypes(), authority, token)
                );
                return;
            }
        } catch (ReflectiveOperationException ignored) {
            // Worker 退出时 AMS 也会清理外部引用；释放失败不覆盖原调用结果。
        }
    }

    private static Object[] externalProviderArguments(
            Class<?>[] types,
            String authority,
            IBinder token
    ) {
        Object[] values = new Object[types.length];
        boolean authorityAssigned = false;
        for (int index = 0; index < types.length; index++) {
            Class<?> type = types[index];
            if (type == String.class) {
                values[index] = authorityAssigned ? "xiaoyv-worker" : authority;
                authorityAssigned = true;
            } else if (type == int.class || type == Integer.class) {
                // Root Worker 当前只服务安装所在的 Android 主用户。
                values[index] = 0;
            } else if (IBinder.class.isAssignableFrom(type)) {
                values[index] = token;
            } else if (type == boolean.class || type == Boolean.class) {
                values[index] = false;
            } else {
                values[index] = null;
            }
        }
        return values;
    }

    private static Bundle invokeProviderCall(
            Object provider,
            Context context,
            String authority,
            String method,
            String arg,
            Bundle extras
    ) throws ReflectiveOperationException {
        for (Method candidate : provider.getClass().getMethods()) {
            if (!"call".equals(candidate.getName())) continue;
            Object[] values = providerCallArguments(
                    candidate.getParameterTypes(),
                    context,
                    authority,
                    method,
                    arg,
                    extras
            );
            if (values == null) continue;
            candidate.setAccessible(true);
            return (Bundle) candidate.invoke(provider, values);
        }
        throw new NoSuchMethodException("IContentProvider.call");
    }

    private static Object[] providerCallArguments(
            Class<?>[] types,
            Context context,
            String authority,
            String method,
            String arg,
            Bundle extras
    ) throws ReflectiveOperationException {
        if (types.length < 4 || !Bundle.class.isAssignableFrom(types[types.length - 1])) {
            return null;
        }
        Object[] values = new Object[types.length];
        values[types.length - 1] = extras;
        int stringCount = 0;
        for (Class<?> type : types) if (type == String.class) stringCount++;

        int stringIndex = 0;
        for (int index = 0; index < types.length - 1; index++) {
            Class<?> type = types[index];
            if (type == String.class) {
                if (stringCount == 3) {
                    values[index] = new String[]{context.getPackageName(), method, arg}[stringIndex];
                } else if (stringCount == 4) {
                    values[index] = new String[]{context.getPackageName(), authority, method, arg}[stringIndex];
                } else if (stringCount == 5) {
                    values[index] = new String[]{context.getPackageName(), null, authority, method, arg}[stringIndex];
                } else {
                    return null;
                }
                stringIndex++;
            } else if (type.getName().equals("android.content.AttributionSource")) {
                values[index] = createRootAttributionSource(type);
            } else {
                return null;
            }
        }
        return values;
    }

    private static Object createRootAttributionSource(Class<?> attributionType)
            throws ReflectiveOperationException {
        Class<?> builderClass = Class.forName("android.content.AttributionSource$Builder");
        Object builder = builderClass.getConstructor(int.class).newInstance(0);
        Method build = builderClass.getMethod("build");
        Object source = build.invoke(builder);
        return attributionType.cast(source);
    }
}
