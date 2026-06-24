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

ImageMetadata storeImageFrame(ImageFrame frame);

bool releaseImageFrame(int imageId);

bool readImagePixel(int imageId, int x, int y, PixelColor* color, std::string* error);

bool readImagePixels(
        int imageId,
        const std::vector<PixelPoint>& points,
        std::vector<int>* colors,
        std::string* error);
