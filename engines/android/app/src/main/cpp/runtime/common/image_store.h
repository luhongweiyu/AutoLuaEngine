/**
 * 文件用途：声明 native 图片帧、屏幕帧缓存和像素读取接口。
 */
#pragma once

#include <cstddef>
#include <string>
#include <vector>

/**
 * Native 图片帧。
 *
 * 屏幕截图进入 native 后统一保存为 RGBA 紧凑内存帧。Lua HostApi、
 * HTTP 协议和后续图色算法都通过图片句柄访问这份内存，避免 PNG 编码、
 * 文件落盘或跨 Java/HTTP 反复传输大块像素。
 */
struct ImageFrame {
    int width = 0;
    int height = 0;
    int rowStride = 0;
    int pixelStride = 0;
    std::string format;
    std::string source;
    long long captureDurationMs = 0;
    std::vector<unsigned char> pixels;
};

struct ImageMetadata {
    int id = 0;
    int width = 0;
    int height = 0;
    int rowStride = 0;
    int pixelStride = 0;
    std::size_t byteLength = 0;
    std::string format;
    std::string source;
    long long captureDurationMs = 0;
};

struct PixelColor {
    int red = 0;
    int green = 0;
    int blue = 0;
    int alpha = 0;
    int rgb = 0;
};

struct PixelPoint {
    int x = 0;
    int y = 0;
};

/**
 * 读取当前屏幕帧缓存。
 *
 * keepCapture 开启时忽略时间限制；未开启时按当前缓冲时间判断是否命中。
 */
bool getCachedScreenFrame(ImageMetadata* metadata);

/**
 * 保存新的屏幕帧，并覆盖上一张屏幕缓存帧。
 */
ImageMetadata storeScreenFrame(ImageFrame frame);

/**
 * 开启锁帧缓存：之后截图一直复用当前屏幕帧，直到 release 或清空缓存。
 */
void keepScreenFrameCache();

/**
 * 关闭锁帧缓存：之后截图恢复为按缓冲时间复用。
 */
void releaseScreenFrameCache();

/**
 * 设置屏幕帧缓冲时间，单位毫秒。传入负数会返回 false。
 */
bool setScreenFrameCacheDurationMs(long long durationMs);

/**
 * 清空屏幕缓存帧，并恢复脚本运行期缓存设置到默认状态。
 */
void clearScreenFrameCache();

bool readImagePixel(int imageId, int x, int y, PixelColor* color, std::string* error);

bool readImagePixels(
        int imageId,
        const std::vector<PixelPoint>& points,
        std::vector<int>* colors,
        std::string* error);
