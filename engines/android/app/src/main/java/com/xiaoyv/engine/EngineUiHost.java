/**
 * 文件用途：在 App UID 控制进程承接一次性 Worker 的脚本 UI/ImGui 请求。
 */
package com.xiaoyv.engine;

import android.content.Context;
import android.content.Intent;

/** Worker 不直接持有 App 组件实例；这里把稳定命令送到真正的 UI 宿主。 */
final class EngineUiHost {
    static final String DIALOG_SHOW = "dialogShow";
    static final String HUD_SHOW = "hudShow";
    static final String HUD_UPDATE = "hudUpdate";
    static final String WEB_SHOW = "webShow";
    static final String WEB_POST = "webPost";
    static final String UI_CLOSE = "uiClose";
    static final String UI_CLOSE_ALL = "uiCloseAll";
    static final String IMGUI_SHOW = "imguiShow";
    static final String IMGUI_UPDATE = "imguiUpdate";
    static final String IMGUI_CLOSE = "imguiClose";
    static final String IMGUI_KEYBOARD = "imguiKeyboard";

    private EngineUiHost() {
    }

    static boolean call(
            Context context,
            String action,
            long sessionId,
            String payload,
            boolean flag
    ) {
        if (context == null || action == null) return false;
        switch (action) {
            case DIALOG_SHOW:
                return ScriptDialogOverlayService.sendCommand(
                        context,
                        ScriptDialogOverlayService.ACTION_SHOW,
                        sessionId,
                        payload
                );
            case HUD_SHOW:
                return ScriptHudService.sendCommand(
                        context,
                        ScriptHudService.ACTION_SHOW,
                        sessionId,
                        payload
                );
            case HUD_UPDATE:
                return ScriptHudService.sendCommand(
                        context,
                        ScriptHudService.ACTION_UPDATE,
                        sessionId,
                        payload
                );
            case WEB_SHOW:
                Intent intent = new Intent(context, ScriptWebActivity.class);
                intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                intent.putExtra(ScriptUiProtocol.EXTRA_SESSION_ID, sessionId);
                intent.putExtra(ScriptUiProtocol.EXTRA_SPEC_JSON, safePayload(payload));
                try {
                    context.startActivity(intent);
                    return true;
                } catch (RuntimeException exception) {
                    return false;
                }
            case WEB_POST:
                ScriptUiProtocol.sendWebMessage(context, sessionId, safePayload(payload));
                return true;
            case UI_CLOSE:
                ScriptUiProtocol.sendClose(context, sessionId);
                ScriptDialogOverlayService.sendCommand(
                        context,
                        ScriptDialogOverlayService.ACTION_CLOSE,
                        sessionId,
                        "{}"
                );
                ScriptHudService.sendCommand(
                        context,
                        ScriptHudService.ACTION_CLOSE,
                        sessionId,
                        "{}"
                );
                return true;
            case UI_CLOSE_ALL:
                closeAll(context);
                return true;
            case IMGUI_SHOW:
                return ScriptImGuiService.sendCommand(
                        context,
                        ScriptImGuiService.ACTION_SHOW,
                        safePayload(payload)
                );
            case IMGUI_UPDATE:
                return ScriptImGuiService.sendCommand(
                        context,
                        ScriptImGuiService.ACTION_UPDATE,
                        safePayload(payload)
                );
            case IMGUI_CLOSE:
                return ScriptImGuiService.sendCommand(
                        context,
                        ScriptImGuiService.ACTION_CLOSE,
                        "{}"
                );
            case IMGUI_KEYBOARD:
                return ScriptImGuiService.sendKeyboardVisible(context, flag);
            default:
                return false;
        }
    }

    static void closeAll(Context context) {
        if (context == null) return;
        ScriptUiProtocol.sendCloseAll(context);
        ScriptDialogOverlayService.sendCommand(
                context,
                ScriptDialogOverlayService.ACTION_CLOSE_ALL,
                0,
                "{}"
        );
        ScriptHudService.sendCommand(
                context,
                ScriptHudService.ACTION_CLOSE_ALL,
                0,
                "{}"
        );
        ScriptImGuiService.sendCommand(
                context,
                ScriptImGuiService.ACTION_CLOSE,
                "{}"
        );
    }

    private static String safePayload(String payload) {
        return payload == null ? "{}" : payload;
    }
}
