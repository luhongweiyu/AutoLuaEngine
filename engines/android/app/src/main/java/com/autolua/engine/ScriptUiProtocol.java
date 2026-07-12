/**
 * 文件用途：定义引擎进程与 App 主进程之间的脚本 UI 控制协议常量。
 */
package com.autolua.engine;

import android.content.Context;
import android.content.Intent;

/**
 * 脚本 UI 跨进程协议。
 *
 * EngineService 位于 :engine，Activity、HUD Service 位于 App 主进程。这里使用包内
 * 显式 Intent/Broadcast 传递界面控制命令，事件再通过本机 EngineHttpServer 回到 native
 * UI 会话队列，避免两个进程直接共享 Java 静态对象。
 */
public final class ScriptUiProtocol {
    public static final String ACTION_CLOSE =
            "com.autolua.engine.action.SCRIPT_UI_CLOSE";
    public static final String ACTION_CLOSE_ALL =
            "com.autolua.engine.action.SCRIPT_UI_CLOSE_ALL";
    public static final String ACTION_WEB_MESSAGE =
            "com.autolua.engine.action.SCRIPT_UI_WEB_MESSAGE";

    public static final String EXTRA_SESSION_ID = "sessionId";
    public static final String EXTRA_SPEC_JSON = "specJson";
    public static final String EXTRA_MESSAGE_JSON = "messageJson";

    private ScriptUiProtocol() {
    }

    /**
     * 向主进程已有的 Activity 广播关闭命令。
     */
    public static void sendClose(Context context, long sessionId) {
        if (context == null) {
            return;
        }
        Intent intent = new Intent(ACTION_CLOSE);
        intent.setPackage(context.getPackageName());
        intent.putExtra(EXTRA_SESSION_ID, sessionId);
        context.sendBroadcast(intent);
    }

    /**
     * 通知主进程关闭全部脚本界面。
     */
    public static void sendCloseAll(Context context) {
        if (context == null) {
            return;
        }
        Intent intent = new Intent(ACTION_CLOSE_ALL);
        intent.setPackage(context.getPackageName());
        context.sendBroadcast(intent);
    }

    /**
     * 向指定 WebView 页面发送 JSON 消息。
     */
    public static void sendWebMessage(Context context, long sessionId, String messageJson) {
        if (context == null) {
            return;
        }
        Intent intent = new Intent(ACTION_WEB_MESSAGE);
        intent.setPackage(context.getPackageName());
        intent.putExtra(EXTRA_SESSION_ID, sessionId);
        intent.putExtra(EXTRA_MESSAGE_JSON, messageJson == null ? "null" : messageJson);
        context.sendBroadcast(intent);
    }
}
