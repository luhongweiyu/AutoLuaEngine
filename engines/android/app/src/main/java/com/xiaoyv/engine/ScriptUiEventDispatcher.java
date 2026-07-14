/**
 * 文件用途：将 App 主进程 UI 事件异步投递回 :engine 的 native UI 会话队列。
 */
package com.xiaoyv.engine;

import android.content.Context;
import android.util.Log;

import org.json.JSONException;
import org.json.JSONObject;
import org.json.JSONTokener;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * 脚本 UI 事件投递器。
 *
 * UI 线程绝不能直接执行 Lua。这里把点击、表单提交、网页消息等事件放到单独线程，
 * 经 EngineHttpServer 的 ui.event 命令写入 libengine.so 的会话队列，再由脚本线程
 * 的 waitEvent 读取。
 */
public final class ScriptUiEventDispatcher {
    private static final String TAG = "小鱼精灵";
    private static final ExecutorService EXECUTOR = Executors.newSingleThreadExecutor();

    private ScriptUiEventDispatcher() {
    }

    /**
     * 投递一个已经结构化的 JSON 数据事件。
     */
    public static void dispatch(Context context, long sessionId, String event, Object data) {
        if (context == null || sessionId <= 0 || event == null || event.isEmpty()) {
            return;
        }

        Context appContext = context.getApplicationContext();
        EXECUTOR.execute(() -> {
            try {
                JSONObject params = new JSONObject();
                params.put("sessionId", sessionId);
                params.put("event", event);
                params.put("data", data == null ? JSONObject.NULL : data);
                EngineLocalClient.call(appContext, "ui.event", params);
            } catch (Exception exception) {
                // UI 可能在脚本已停止后才收到最后一个生命周期回调，此时事件被 native
                // 丢弃即可，不需要重新拉起引擎或重新打开页面。
                Log.i(TAG, "已忽略脚本 UI 事件：" + exception.getMessage());
            }
        });
    }

    /**
     * 投递来自 WebView JavaScript 的 JSON 文本。
     *
     * 页面可传任意 JSON 值；不是 JSON 时按普通字符串原样交给脚本，避免网页侧因为
     * 忘记 JSON.stringify 而丢失一条交互消息。
     */
    public static void dispatchJson(Context context, long sessionId, String event, String dataJson) {
        Object data = JSONObject.NULL;
        if (dataJson != null && !dataJson.trim().isEmpty()) {
            try {
                data = new JSONTokener(dataJson).nextValue();
            } catch (JSONException ignored) {
                data = dataJson;
            }
        }
        dispatch(context, sessionId, event, data);
    }
}
