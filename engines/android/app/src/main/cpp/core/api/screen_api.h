/**
 * 文件用途：声明屏幕截图核心 API，负责缓存、锁帧和 Root 截图分发。
 */
#pragma once

#include <string>

namespace autolua::api {

/**
 * 屏幕截图结果。
 *
 * pixels 指向 libengine.so 内部缓存，调用方只读，不释放。下一次缓存过期重新
 * 截图、clearScreenCaptureCache 或脚本结束清理时，该地址可能失效。
 */
struct ScreenFrame {
    int width = 0;
    int height = 0;
    unsigned char* pixels = nullptr;
    long long frameId = 0;
};

/**
 * 获取屏幕截图。
 *
 * 返回 true 表示 frame 已写入；返回 false 可通过 screenLastError 读取错误。
 */
bool captureScreen(ScreenFrame* frame);

/**
 * 锁定当前截图帧。
 */
void keepScreenCapture();

/**
 * 取消锁帧，恢复按缓存时间判断是否重新截图。
 */
void releaseScreenCapture();

/**
 * 设置截图缓存时间，单位毫秒。
 */
bool setScreenCaptureCacheMs(int durationMs);

/**
 * 清空截图缓存，并恢复默认缓存时间和非锁帧状态。
 */
void clearScreenCaptureCache();

/**
 * 返回最近一次截图核心 API 失败原因。
 */
std::string screenLastError();

/**
 * 返回当前截图缓存帧编号。
 *
 * 每次写入新截图或清空截图缓存都会递增。找色转置缓存用这个编号判断自己是否
 * 还对应当前截图帧。
 */
long long screenCaptureFrameId();

} // namespace autolua::api
