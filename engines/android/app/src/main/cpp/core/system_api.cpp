/**
 * 文件用途：实现 native 统一系统 API，向引擎层屏蔽 Android 平台桥细节。
 */
#include "system_api.h"

namespace autolua::core {

void SystemApi::init(JavaVM* javaVm) {
    AndroidBridge::init(javaVm);
}

bool SystemApi::isAccessibilityEnabled() {
    return AndroidBridge::isAccessibilityEnabled();
}

int SystemApi::apiLevel() {
    return AndroidBridge::apiLevel();
}

int SystemApi::httpPort() {
    return AndroidBridge::httpPort();
}

std::string SystemApi::packageName() {
    return AndroidBridge::packageName();
}

bool SystemApi::isRootModeEnabled() {
    return AndroidBridge::isRootModeEnabled();
}

bool SystemApi::setRootModeEnabled(bool enabled) {
    return AndroidBridge::setRootModeEnabled(enabled);
}

bool SystemApi::isRootAvailable() {
    return AndroidBridge::isRootAvailable();
}

bool SystemApi::isRootRuntimeReady() {
    return AndroidBridge::isRootRuntimeReady();
}

bool SystemApi::prepareRootRuntime() {
    return AndroidBridge::prepareRootRuntime();
}

bool SystemApi::prepareRootHelper() {
    return AndroidBridge::prepareRootHelper();
}

RootStatusResult SystemApi::rootStatus() {
    return AndroidBridge::rootStatus();
}

RootExecResult SystemApi::rootExec(const std::string& command, int timeoutMs) {
    return AndroidBridge::rootExec(command, timeoutMs);
}

RootExecResult SystemApi::rootFileExists(const std::string& path) {
    return AndroidBridge::rootFileExists(path);
}

RootExecResult SystemApi::rootFileReadText(const std::string& path, int timeoutMs) {
    return AndroidBridge::rootFileReadText(path, timeoutMs);
}

RootExecResult SystemApi::rootFileWriteText(
        const std::string& path,
        const std::string& content,
        int timeoutMs) {
    return AndroidBridge::rootFileWriteText(path, content, timeoutMs);
}

RootExecResult SystemApi::rootFileStat(const std::string& path) {
    return AndroidBridge::rootFileStat(path);
}

RootExecResult SystemApi::rootFileList(const std::string& path) {
    return AndroidBridge::rootFileList(path);
}

RootExecResult SystemApi::rootFileRemove(const std::string& path, bool recursive) {
    return AndroidBridge::rootFileRemove(path, recursive);
}

RootExecResult SystemApi::rootFileMkdir(const std::string& path, bool recursive) {
    return AndroidBridge::rootFileMkdir(path, recursive);
}

RootExecResult SystemApi::rootFileChmod(const std::string& path, const std::string& mode) {
    return AndroidBridge::rootFileChmod(path, mode);
}

RootExecResult SystemApi::rootFileChown(const std::string& path, const std::string& owner) {
    return AndroidBridge::rootFileChown(path, owner);
}

RootExecResult SystemApi::rootProcessPidOf(const std::string& processName) {
    return AndroidBridge::rootProcessPidOf(processName);
}

RootExecResult SystemApi::rootProcessList() {
    return AndroidBridge::rootProcessList();
}

RootExecResult SystemApi::rootProcessInfo(const std::string& pidOrName) {
    return AndroidBridge::rootProcessInfo(pidOrName);
}

RootExecResult SystemApi::rootProcessStats(const std::string& pidOrName) {
    return AndroidBridge::rootProcessStats(pidOrName);
}

RootExecResult SystemApi::rootProcessKill(const std::string& pidOrName, int signal) {
    return AndroidBridge::rootProcessKill(pidOrName, signal);
}

RootExecResult SystemApi::deviceScreenState() {
    return AndroidBridge::deviceScreenState();
}

RootExecResult SystemApi::deviceWake() {
    return AndroidBridge::deviceWake();
}

RootExecResult SystemApi::deviceSleep() {
    return AndroidBridge::deviceSleep();
}

RootExecResult SystemApi::deviceBattery() {
    return AndroidBridge::deviceBattery();
}

RootExecResult SystemApi::deviceRotation() {
    return AndroidBridge::deviceRotation();
}

RootExecResult SystemApi::deviceSetRotation(int rotation, bool locked) {
    return AndroidBridge::deviceSetRotation(rotation, locked);
}

RootExecResult SystemApi::deviceSettingsGet(
        const std::string& namespaceName,
        const std::string& key) {
    return AndroidBridge::deviceSettingsGet(namespaceName, key);
}

RootExecResult SystemApi::deviceSettingsPut(
        const std::string& namespaceName,
        const std::string& key,
        const std::string& value) {
    return AndroidBridge::deviceSettingsPut(namespaceName, key, value);
}

RootExecResult SystemApi::deviceSettingsDelete(
        const std::string& namespaceName,
        const std::string& key) {
    return AndroidBridge::deviceSettingsDelete(namespaceName, key);
}

RootExecResult SystemApi::devicePropGet(const std::string& key) {
    return AndroidBridge::devicePropGet(key);
}

RootExecResult SystemApi::devicePropSet(const std::string& key, const std::string& value) {
    return AndroidBridge::devicePropSet(key, value);
}

RootExecResult SystemApi::deviceDisplayInfo() {
    return AndroidBridge::deviceDisplayInfo();
}

RootExecResult SystemApi::deviceDisplaySetSize(int width, int height) {
    return AndroidBridge::deviceDisplaySetSize(width, height);
}

RootExecResult SystemApi::deviceDisplayResetSize() {
    return AndroidBridge::deviceDisplayResetSize();
}

RootExecResult SystemApi::deviceDisplaySetDensity(int density) {
    return AndroidBridge::deviceDisplaySetDensity(density);
}

RootExecResult SystemApi::deviceDisplayResetDensity() {
    return AndroidBridge::deviceDisplayResetDensity();
}

RootExecResult SystemApi::deviceDisplaySetBrightness(int brightness) {
    return AndroidBridge::deviceDisplaySetBrightness(brightness);
}

RootExecResult SystemApi::deviceDisplaySetAutoBrightness(bool enabled) {
    return AndroidBridge::deviceDisplaySetAutoBrightness(enabled);
}

bool SystemApi::appIsInstalled(const std::string& packageName) {
    return AndroidBridge::appIsInstalled(packageName);
}

bool SystemApi::appOpen(const std::string& packageName) {
    return AndroidBridge::appOpen(packageName);
}

bool SystemApi::appStop(const std::string& packageName) {
    return AndroidBridge::appStop(packageName);
}

bool SystemApi::appClearData(const std::string& packageName) {
    return AndroidBridge::appClearData(packageName);
}

bool SystemApi::appGrantPermission(
        const std::string& packageName,
        const std::string& permissionName) {
    return AndroidBridge::appGrantPermission(packageName, permissionName);
}

bool SystemApi::appRevokePermission(
        const std::string& packageName,
        const std::string& permissionName) {
    return AndroidBridge::appRevokePermission(packageName, permissionName);
}

RootExecResult SystemApi::appCurrent() {
    return AndroidBridge::appCurrent();
}

bool SystemApi::appInstall(const std::string& apkPath, bool replace) {
    return AndroidBridge::appInstall(apkPath, replace);
}

bool SystemApi::appUninstall(const std::string& packageName, bool keepData) {
    return AndroidBridge::appUninstall(packageName, keepData);
}

bool SystemApi::appDisable(const std::string& packageName) {
    return AndroidBridge::appDisable(packageName);
}

bool SystemApi::appEnable(const std::string& packageName) {
    return AndroidBridge::appEnable(packageName);
}

bool SystemApi::appDisableComponent(const std::string& componentName) {
    return AndroidBridge::appDisableComponent(componentName);
}

bool SystemApi::appEnableComponent(const std::string& componentName) {
    return AndroidBridge::appEnableComponent(componentName);
}

bool SystemApi::hasScreenCapturePermission() {
    return AndroidBridge::hasScreenCapturePermission();
}

ScreenCaptureResult SystemApi::captureScreen() {
    return AndroidBridge::captureScreen();
}

ScreenCaptureResult SystemApi::captureRootScreen() {
    return AndroidBridge::captureRootScreen();
}

bool SystemApi::touchTap(int x, int y) {
    return AndroidBridge::touchTap(x, y);
}

bool SystemApi::touchSwipe(int x1, int y1, int x2, int y2, int durationMs) {
    return AndroidBridge::touchSwipe(x1, y1, x2, y2, durationMs);
}

bool SystemApi::inputText(const std::string& text) {
    return AndroidBridge::inputText(text);
}

bool SystemApi::pasteText(const std::string& text) {
    return AndroidBridge::pasteText(text);
}

bool SystemApi::keyPress(int keyCode) {
    return AndroidBridge::keyPress(keyCode);
}

bool SystemApi::keyBack() {
    return AndroidBridge::keyBack();
}

bool SystemApi::keyHome() {
    return AndroidBridge::keyHome();
}

} // namespace autolua::core
