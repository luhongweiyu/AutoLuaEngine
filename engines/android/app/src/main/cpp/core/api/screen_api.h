/**
 * 文件用途：声明屏幕点阵核心 API，负责物理截图缓存、锁帧和图片屏幕切换。
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace xiaoyv::api {

/**
 * 屏幕截图结果。
 *
 * pixels 指向 libengine.so 内部缓存，调用方只读，不释放。缓存仅在当前脚本任务
 * 中保留；脚本结束或分辨率变化导致缓存扩容时，地址可能失效。
 */
struct ScreenFrame {
    int width = 0;
    int height = 0;
    unsigned char* pixels = nullptr;
    long long frameId = 0;
    // 图片屏幕使用共享所有权，保证还原或替换发生时，正在执行的图像算法仍可安全读完旧点阵。
    // 普通系统截图仍由原有全局缓冲持有，此字段为空。
    std::shared_ptr<const std::vector<unsigned char>> pixelOwner;
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
 * 把一张紧凑 RGBA 图片设置为当前活动屏幕点阵。
 *
 * 图片宽高不得超过物理屏幕。设置后 captureScreen 始终返回该图片，不受截图缓存时间影响；
 * 原系统截图缓冲保持不变。函数接管 pixels 的内存，不再复制整张图片。
 */
bool setScreenPixelOverride(
        std::vector<unsigned char>&& pixels,
        int width,
        int height,
        int screenWidth,
        int screenHeight
);

/**
 * 还原系统屏幕点阵。
 *
 * 当前读取者通过 ScreenFrame::pixelOwner 保持旧图片存活；最后一个读取者结束后图片内存释放。
 */
void restoreScreenPixelOverride();

/**
 * 释放当前脚本任务的全部屏幕点阵。
 *
 * 这是引擎任务收尾专用函数，不通过 C ABI 或语言绑定公开。它会同时释放物理截图缓存和
 * 图片屏幕，并恢复默认缓存设置。
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
