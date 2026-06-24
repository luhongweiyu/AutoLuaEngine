package com.autolua.engine;

import android.content.Context;

/**
 * Java 平台能力桥。
 *
 * Native HostApi 需要调用 Android 系统能力时，统一通过这里进入 Java 层。
 * 这样 JNI 只依赖一个稳定类，不直接散落调用 Activity 或 Service。
 */
public final class AndroidHostBridge {
    private static Context appContext;

    private AndroidHostBridge() {
    }

    public static void init(Context context) {
        appContext = context.getApplicationContext();
    }

    public static boolean isAccessibilityEnabled() {
        return AutomationAccessibilityService.isEnabled();
    }

    public static boolean isRootAvailable() {
        return RootShellBridge.isRootAvailable();
    }

    public static RootStatus rootStatus() {
        return RootShellBridge.status();
    }

    public static boolean isRootModeEnabled() {
        return isRootModeEnabledInternal();
    }

    public static boolean setRootModeEnabled(boolean enabled) {
        if (appContext == null) {
            return false;
        }
        EngineSettings.setRootModeEnabled(appContext, enabled);
        return true;
    }

    public static RootCommandResult rootExec(String command, int timeoutMs) {
        return RootShellBridge.exec(command, timeoutMs);
    }

    public static RootCommandResult rootFileExists(String path) {
        return RootShellBridge.fileExists(path);
    }

    public static RootCommandResult rootFileReadText(String path, int timeoutMs) {
        return RootShellBridge.readTextFile(path, timeoutMs);
    }

    public static RootCommandResult rootFileWriteText(String path, String content, int timeoutMs) {
        return RootShellBridge.writeTextFile(path, content, timeoutMs);
    }

    public static RootCommandResult rootFileStat(String path) {
        return RootShellBridge.statFile(path);
    }

    public static RootCommandResult rootFileList(String path) {
        return RootShellBridge.listFiles(path);
    }

    public static RootCommandResult rootFileRemove(String path, boolean recursive) {
        return RootShellBridge.removeFile(path, recursive);
    }

    public static RootCommandResult rootFileMkdir(String path, boolean recursive) {
        return RootShellBridge.makeDirectory(path, recursive);
    }

    public static RootCommandResult rootFileChmod(String path, String mode) {
        return RootShellBridge.chmod(path, mode);
    }

    public static RootCommandResult rootFileChown(String path, String owner) {
        return RootShellBridge.chown(path, owner);
    }

    public static RootCommandResult rootProcessPidOf(String processName) {
        return RootShellBridge.pidOf(processName);
    }

    public static RootCommandResult rootProcessList() {
        return RootShellBridge.listProcesses();
    }

    public static RootCommandResult rootProcessInfo(String pidOrName) {
        return RootShellBridge.processInfo(pidOrName);
    }

    public static RootCommandResult rootProcessKill(String pidOrName, int signal) {
        return RootShellBridge.killProcess(pidOrName, signal);
    }

    public static boolean appIsInstalled(String packageName) {
        return appContext != null && AppControlBridge.isInstalled(appContext, packageName);
    }

    public static boolean appOpen(String packageName) {
        return appContext != null
                && AppControlBridge.open(appContext, packageName, isRootModeEnabledInternal());
    }

    public static boolean appStop(String packageName) {
        return AppControlBridge.stop(packageName);
    }

    public static boolean appClearData(String packageName) {
        return AppControlBridge.clearData(packageName);
    }

    public static boolean appGrantPermission(String packageName, String permissionName) {
        return AppControlBridge.grantPermission(packageName, permissionName);
    }

    public static boolean appRevokePermission(String packageName, String permissionName) {
        return AppControlBridge.revokePermission(packageName, permissionName);
    }

    public static RootCommandResult appCurrent() {
        return AppControlBridge.current();
    }

    public static boolean appInstall(String apkPath, boolean replace) {
        return AppControlBridge.install(apkPath, replace);
    }

    public static boolean appUninstall(String packageName, boolean keepData) {
        return AppControlBridge.uninstall(packageName, keepData);
    }

    public static boolean appDisable(String packageName) {
        return AppControlBridge.disable(packageName);
    }

    public static boolean appEnable(String packageName) {
        return AppControlBridge.enable(packageName);
    }

    public static boolean touchTap(int x, int y) {
        if (isRootModeEnabledInternal() && RootShellBridge.tap(x, y)) {
            return true;
        }
        return AutomationAccessibilityService.tap(x, y);
    }

    public static boolean touchSwipe(int x1, int y1, int x2, int y2, int durationMs) {
        if (isRootModeEnabledInternal() && RootShellBridge.swipe(x1, y1, x2, y2, durationMs)) {
            return true;
        }
        return AutomationAccessibilityService.swipe(x1, y1, x2, y2, durationMs);
    }

    public static boolean keyBack() {
        if (isRootModeEnabledInternal() && RootShellBridge.keyBack()) {
            return true;
        }
        return AutomationAccessibilityService.back();
    }

    public static boolean keyHome() {
        if (isRootModeEnabledInternal() && RootShellBridge.keyHome()) {
            return true;
        }
        return AutomationAccessibilityService.home();
    }

    /**
     * 执行通用 Android 按键码。
     *
     * 第一版优先走 root input keyevent；Back/Home 这两个系统键在 root 不可用时
     * 可以回退无障碍全局动作。其他 keyCode 暂不做模拟兜底，避免不同机型行为不一致。
     */
    public static boolean keyPress(int keyCode) {
        if (isRootModeEnabledInternal() && RootShellBridge.keyEvent(keyCode)) {
            return true;
        }
        if (keyCode == 4) {
            return AutomationAccessibilityService.back();
        }
        if (keyCode == 3) {
            return AutomationAccessibilityService.home();
        }
        return false;
    }

    /**
     * 向当前焦点输入框输入文本。
     *
     * 第一优先级仍然是 root `input text`，它对英文、数字和简单空格最快；
     * 失败后回退剪贴板 + root 粘贴，覆盖中文、换行和复杂符号。
     */
    public static boolean inputText(String text) {
        if (!isRootModeEnabledInternal() || text == null) {
            return false;
        }
        if (RootShellBridge.inputText(text)) {
            return true;
        }
        return pasteText(text);
    }

    /**
     * 通过剪贴板向当前焦点输入文本。
     *
     * 该路线会修改系统剪贴板内容，并依赖当前焦点控件支持粘贴。它比 `input text`
     * 更适合中文、换行和特殊字符，但不适合需要保留用户剪贴板内容的场景。
     */
    public static boolean pasteText(String text) {
        return appContext != null
                && isRootModeEnabledInternal()
                && ClipboardBridge.setText(appContext, text)
                && RootShellBridge.paste();
    }

    public static boolean hasScreenCapturePermission() {
        return (isRootModeEnabledInternal() && RootShellBridge.isRootAvailable())
                || ScreenCaptureBridge.hasPermission();
    }

    public static ScreenCaptureResult captureScreen() {
        if (isRootModeEnabledInternal() && RootShellBridge.isRootAvailable()) {
            ScreenCaptureResult rootCapture = RootScreenCaptureBridge.captureFrame();
            if (rootCapture.success || !ScreenCaptureBridge.hasPermission()) {
                return rootCapture;
            }
        }
        return ScreenCaptureBridge.captureFrame();
    }

    private static boolean isRootModeEnabledInternal() {
        return appContext == null || EngineSettings.isRootModeEnabled(appContext);
    }
}
