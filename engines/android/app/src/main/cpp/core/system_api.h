/**
 * 文件用途：声明 native 统一系统 API，供脚本运行时和命令分发层调用。
 */
#pragma once

#include <jni.h>
#include <string>

#include "../platform/android_bridge.h"

namespace autolua::core {

/**
 * native 核心系统 API 出口。
 *
 * 这一层面向 Lua、后续 JS、IDE 协议和插件复用；具体 Android Java/root
 * 细节仍由 platform/android_bridge.* 处理。这样脚本语言层只需要绑定
 * SystemApi，不需要知道底层是无障碍、root shell 还是系统服务。
 */
class SystemApi {
public:
    static void init(JavaVM* javaVm);

    static bool isAccessibilityEnabled();
    static int apiLevel();
    static int httpPort();
    static std::string packageName();
    static bool isRootModeEnabled();
    static bool setRootModeEnabled(bool enabled);
    static bool isRootAvailable();
    static bool isRootRuntimeReady();
    static bool prepareRootRuntime();
    static bool prepareRootHelper();
    static RootStatusResult rootStatus();
    static RootExecResult rootExec(const std::string& command, int timeoutMs);

    static RootExecResult rootFileExists(const std::string& path);
    static RootExecResult rootFileReadText(const std::string& path, int timeoutMs);
    static RootExecResult rootFileWriteText(
            const std::string& path,
            const std::string& content,
            int timeoutMs
    );
    static RootExecResult rootFileStat(const std::string& path);
    static RootExecResult rootFileList(const std::string& path);
    static RootExecResult rootFileRemove(const std::string& path, bool recursive);
    static RootExecResult rootFileMkdir(const std::string& path, bool recursive);
    static RootExecResult rootFileChmod(const std::string& path, const std::string& mode);
    static RootExecResult rootFileChown(const std::string& path, const std::string& owner);

    static RootExecResult rootProcessPidOf(const std::string& processName);
    static RootExecResult rootProcessList();
    static RootExecResult rootProcessInfo(const std::string& pidOrName);
    static RootExecResult rootProcessStats(const std::string& pidOrName);
    static RootExecResult rootProcessKill(const std::string& pidOrName, int signal);

    static RootExecResult deviceScreenState();
    static RootExecResult deviceWake();
    static RootExecResult deviceSleep();
    static RootExecResult deviceBattery();
    static RootExecResult deviceRotation();
    static RootExecResult deviceSetRotation(int rotation, bool locked);
    static RootExecResult deviceSettingsGet(const std::string& namespaceName, const std::string& key);
    static RootExecResult deviceSettingsPut(
            const std::string& namespaceName,
            const std::string& key,
            const std::string& value
    );
    static RootExecResult deviceSettingsDelete(
            const std::string& namespaceName,
            const std::string& key
    );
    static RootExecResult devicePropGet(const std::string& key);
    static RootExecResult devicePropSet(const std::string& key, const std::string& value);
    static RootExecResult deviceDisplayInfo();
    static RootExecResult deviceDisplaySetSize(int width, int height);
    static RootExecResult deviceDisplayResetSize();
    static RootExecResult deviceDisplaySetDensity(int density);
    static RootExecResult deviceDisplayResetDensity();
    static RootExecResult deviceDisplaySetBrightness(int brightness);
    static RootExecResult deviceDisplaySetAutoBrightness(bool enabled);

    static bool appIsInstalled(const std::string& packageName);
    static bool appOpen(const std::string& packageName);
    static bool appStop(const std::string& packageName);
    static bool appClearData(const std::string& packageName);
    static bool appGrantPermission(
            const std::string& packageName,
            const std::string& permissionName
    );
    static bool appRevokePermission(
            const std::string& packageName,
            const std::string& permissionName
    );
    static RootExecResult appCurrent();
    static bool appInstall(const std::string& apkPath, bool replace);
    static bool appUninstall(const std::string& packageName, bool keepData);
    static bool appDisable(const std::string& packageName);
    static bool appEnable(const std::string& packageName);
    static bool appDisableComponent(const std::string& componentName);
    static bool appEnableComponent(const std::string& componentName);

    static bool hasScreenCapturePermission();
    static ScreenCaptureResult captureScreen();
    static ScreenCaptureResult captureRootScreen();
    static bool touchTap(int x, int y);
    static bool touchSwipe(int x1, int y1, int x2, int y2, int durationMs);
    static bool inputText(const std::string& text);
    static bool pasteText(const std::string& text);
    static bool keyPress(int keyCode);
    static bool keyBack();
    static bool keyHome();
};

} // namespace autolua::core
