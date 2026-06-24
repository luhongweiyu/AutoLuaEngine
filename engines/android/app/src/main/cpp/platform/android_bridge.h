#pragma once

#include <jni.h>
#include <string>
#include <vector>

struct ScreenCaptureResult {
    bool success = false;
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    int rowStride = 0;
    int pixelStride = 0;
    std::string format;
    std::string error;
};

struct RootExecResult {
    bool success = false;
    int exitCode = -1;
    std::string stdoutText;
    std::string stderrText;
    bool timedOut = false;
    std::string error;
};

struct RootProbeAttempt {
    std::string commandMode;
    std::string suPath;
    int exitCode = -1;
    std::string stdoutText;
    std::string stderrText;
    bool timedOut = false;
    std::string error;
};

struct RootStatusResult {
    bool available = false;
    std::string commandMode;
    std::string suPath;
    bool cached = false;
    long long cacheExpireAt = 0;
    std::string error;
    std::vector<RootProbeAttempt> attempts;
};

/**
 * Android 平台能力桥。
 *
 * Native 层需要调用 Java/Kotlin 系统能力时统一从这里进入。
 * 当前用于 root / 无障碍状态检测、截图和触控按键。
 */
class AndroidBridge {
public:
    static void init(JavaVM* javaVm);

    static bool isAccessibilityEnabled();
    static bool isRootModeEnabled();
    static bool setRootModeEnabled(bool enabled);
    static bool isRootAvailable();
    static RootStatusResult rootStatus();
    static RootExecResult rootExec(const std::string& command, int timeoutMs);
    static RootExecResult rootFileExists(const std::string& path);
    static RootExecResult rootFileReadText(const std::string& path, int timeoutMs);
    static RootExecResult rootFileWriteText(
            const std::string& path,
            const std::string& content,
            int timeoutMs
    );
    static RootExecResult rootFileRemove(const std::string& path);
    static RootExecResult rootFileMkdir(const std::string& path, bool recursive);
    static RootExecResult rootFileChmod(const std::string& path, const std::string& mode);
    static RootExecResult rootProcessPidOf(const std::string& processName);
    static RootExecResult rootProcessKill(const std::string& pidOrName, int signal);
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
    static bool appInstall(const std::string& apkPath, bool replace);
    static bool appUninstall(const std::string& packageName, bool keepData);
    static bool appDisable(const std::string& packageName);
    static bool appEnable(const std::string& packageName);
    static bool hasScreenCapturePermission();
    static ScreenCaptureResult captureScreen();
    static bool touchTap(int x, int y);
    static bool touchSwipe(int x1, int y1, int x2, int y2, int durationMs);
    static bool inputText(const std::string& text);
    static bool pasteText(const std::string& text);
    static bool keyPress(int keyCode);
    static bool keyBack();
    static bool keyHome();

private:
};
