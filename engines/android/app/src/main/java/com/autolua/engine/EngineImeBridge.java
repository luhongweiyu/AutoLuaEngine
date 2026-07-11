/**
 * 文件用途：管理 AutoLuaEngine 输入法的锁定、文本提交和原输入法恢复。
 */
package com.autolua.engine;

import android.content.Context;
import android.content.SharedPreferences;

/**
 * 输入法控制桥。
 *
 * lock 通过常驻 root helper 启用并切换本应用输入法，同时保存原默认输入法；
 * setText 直接调用本应用 InputMethodService；unlock 恢复保存的原输入法并取消
 * 本应用输入法启用状态。三者都没有无障碍或按键注入回退路线。
 */
public final class EngineImeBridge {
    private static final Object LOCK = new Object();

    private static final String PREF_NAME = "ime_lock_state";
    private static final String KEY_LOCKED = "locked";
    private static final String KEY_PREVIOUS_INPUT_METHOD = "previous_input_method";
    private static final String INPUT_METHOD_COMPONENT =
            "com.autolua.engine/.EngineInputMethodService";

    private EngineImeBridge() {
    }

    /**
     * 返回 Manifest 中声明的输入法组件名。
     *
     * Root helper 和普通引擎进程都从这里取同一个常量，避免输入法服务名称在两端漂移。
     */
    static String inputMethodComponent() {
        return INPUT_METHOD_COMPONENT;
    }

    /**
     * 记录当前默认输入法并切换到 AutoLuaEngine 输入法。
     *
     * 重复调用不会覆盖首次保存的原输入法，只重新确保当前输入法已切换到本服务。
     */
    public static boolean lock() {
        Context context = AndroidHostBridge.applicationContext();
        if (context == null) {
            return false;
        }

        synchronized (LOCK) {
            SharedPreferences preferences = preferences(context);
            if (preferences.getBoolean(KEY_LOCKED, false)) {
                return RootHelperBridge.lockInputMethod(INPUT_METHOD_COMPONENT) != null;
            }

            String previousInputMethod = RootHelperBridge.lockInputMethod(
                    INPUT_METHOD_COMPONENT
            );
            if (previousInputMethod == null
                    || previousInputMethod.isEmpty()
                    || INPUT_METHOD_COMPONENT.equals(previousInputMethod)) {
                return false;
            }

            return preferences.edit()
                    .putBoolean(KEY_LOCKED, true)
                    .putString(KEY_PREVIOUS_INPUT_METHOD, previousInputMethod)
                    .commit();
        }
    }

    /**
     * 通过当前活动的 AutoLuaEngine 输入法提交文本。
     */
    public static boolean setText(String text) {
        Context context = AndroidHostBridge.applicationContext();
        if (context == null || !preferences(context).getBoolean(KEY_LOCKED, false)) {
            return false;
        }
        return EngineInputMethodService.commitText(text);
    }

    /**
     * 恢复 lock 前保存的默认输入法，并禁用 AutoLuaEngine 输入法。
     */
    public static boolean unlock() {
        Context context = AndroidHostBridge.applicationContext();
        if (context == null) {
            return false;
        }

        synchronized (LOCK) {
            SharedPreferences preferences = preferences(context);
            if (!preferences.getBoolean(KEY_LOCKED, false)) {
                return true;
            }

            String previousInputMethod = preferences.getString(
                    KEY_PREVIOUS_INPUT_METHOD,
                    ""
            );
            if (previousInputMethod == null || previousInputMethod.isEmpty()) {
                return false;
            }

            if (!RootHelperBridge.unlockInputMethod(
                    previousInputMethod,
                    INPUT_METHOD_COMPONENT
            )) {
                return false;
            }

            return preferences.edit()
                    .remove(KEY_LOCKED)
                    .remove(KEY_PREVIOUS_INPUT_METHOD)
                    .commit();
        }
    }

    /**
     * 取得当前进程的输入法锁定状态存储。
     */
    private static SharedPreferences preferences(Context context) {
        return context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE);
    }
}
