/**
 * 文件用途：把音量键事件转换为运行当前脚本或停止脚本的统一 App 控制命令。
 */
package com.xiaoyv.engine;

import android.content.Context;
import android.view.KeyEvent;

/**
 * 音量键脚本控制器。
 *
 * Root 输入监听和无障碍按键监听最终都进入这里。此类只负责读取当前选择并发送控制 Intent，
 * 不直接接触 Lua VM，也不在 App 主进程执行脚本。
 */
final class VolumeKeyController {
    private VolumeKeyController() {
    }

    /**
     * 处理一次已经去除长按重复事件的音量键按下。
     *
     * 音量加运行脚本列表中当前选中的可运行文件；音量减始终发送停止命令。引擎自身负责
     * 判断是否已有脚本正在运行，避免 App 与悬浮窗各自维护另一份任务状态。
     */
    static void handleKeyDown(Context context, int keyCode) {
        if (context == null || !EngineSettings.isVolumeKeyControlEnabled(context)) {
            return;
        }

        Context appContext = context.getApplicationContext();
        if (keyCode == KeyEvent.KEYCODE_VOLUME_DOWN) {
            EngineService.stopScript(appContext);
            return;
        }
        if (keyCode != KeyEvent.KEYCODE_VOLUME_UP) {
            return;
        }

        ScriptCatalog.ScriptItem selectedScript = ScriptCatalog.getSelectedScript(appContext);
        if (selectedScript == null || !selectedScript.runnable) {
            return;
        }
        EngineService.runScriptFile(appContext, selectedScript.filePath);
    }
}
