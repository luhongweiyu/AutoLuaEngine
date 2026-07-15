/**
 * 文件用途：声明模板找图与截图保存核心 API，统一复用当前 Root 截图缓存。
 */
#pragma once

#include <string>

namespace xiaoyv::api {

/** 找图命中坐标。未找到或调用失败时 x/y 均为 -1。 */
struct 找图坐标 {
    int x = -1;
    int y = -1;
};

/**
 * 在当前截图缓存中查找模板图片。
 *
 * picName 支持绝对图片路径、当前脚本目录下的相对路径，以及当前 ALPKG 包中的资源相对路径。
 * deltaColor 是 RGB 单通道容差，例如 "101010"；sim 是 0.0 到 1.0 的像素匹配比例。
 */
bool 在屏幕中找图(
        int x1,
        int y1,
        int x2,
        int y2,
        const char* picName,
        const char* deltaColor,
        int direction,
        double similarity,
        找图坐标* point
);

/**
 * 把当前截图缓存保存为图片文件。
 *
 * 只有显式调用时才进行 RGBA 到 PNG/JPEG/WebP 编码，正常 capture/findPic 不会产生文件 IO。
 */
bool 保存当前截图(const char* path);

/** 清理全部模板缓存，或清理指定图片路径对应的模板缓存。 */
void 清理图片缓存(const char* picName);

/** 返回当前线程最近一次图片 API 失败原因。 */
std::string 取图片错误();

} // namespace xiaoyv::api
