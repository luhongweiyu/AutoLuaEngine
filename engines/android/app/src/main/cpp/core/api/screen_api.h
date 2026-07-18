/**
 * 文件用途：声明屏幕点阵核心 API，负责物理截图缓存、锁帧和图片屏幕切换。
 */
#pragma once

#include <string>
#include <vector>

namespace xiaoyv::api {

/**
 * 屏幕截图结果。
 *
 * pixels 指向 libengine.so 当前脚本任务的固定缓冲区，调用方只读、不释放。物理截图和
 * setScreenPixels 共用该地址；内容可以被后续截图或图片设置覆盖，任务结束后地址失效。
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
 * 把一张紧凑 RGBA 图片复制到固定屏幕缓冲区，并设为当前活动屏幕点阵。
 *
 * 图片宽高不得超过物理屏幕。设置后 captureScreen 始终返回该图片，不受截图缓存时间影响；
 * 图片和物理截图返回同一地址。函数返回后传入的 pixels 可以正常释放。
 */
bool setScreenPixelOverride(
        std::vector<unsigned char>&& pixels,
        int width,
        int height,
        int screenWidth,
        int screenHeight
);

/**
 * 还原系统屏幕点阵。该函数只关闭图片屏幕并使物理帧失效；下一次读取会把实时
 * Root 截图写入同一块固定缓冲区。
 */
void restoreScreenPixelOverride();

/**
 * 清理当前脚本任务的全部屏幕点阵状态。
 *
 * 这是引擎任务收尾专用函数，不通过 C ABI 或语言绑定公开。它会释放固定缓冲区，清除
 * 物理帧和图片屏幕状态，并恢复默认缓存设置。
 */
void clearScreenCaptureCache();

/**
 * 返回最近一次截图核心 API 失败原因。
 */
std::string screenLastError();

/**
 * 返回当前截图缓存帧编号。
 *
 * 每次写入新截图都会递增。找色转置缓存用这个编号判断自己是否还对应当前截图帧。
 */
long long screenCaptureFrameId();

} // namespace xiaoyv::api
