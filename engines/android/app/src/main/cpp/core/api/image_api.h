/**
 * 文件用途：声明模板找图与截图保存核心 API，统一复用当前 Root 截图缓存。
 */
#pragma once

#include <cstddef>
#include <string>

namespace xiaoyv::api {

/** 找图命中坐标。未找到或调用失败时 x/y 均为 -1。 */
struct 找图坐标 {
    int x = -1;
    int y = -1;
};

/**
 * 截图保存区域，采用左闭右开坐标。
 *
 * 例如 left=10、top=20、right=110、bottom=70 时，输出图片尺寸为 100x50。
 */
struct 截图区域 {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
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
 * 把当前截图缓存的指定区域保存为图片文件。
 *
 * region 为 nullptr 时保存完整屏幕；非空时必须完全位于屏幕内且宽高大于 0。
 * 只有显式调用 capture(path) 时才进行 RGBA 到 PNG/JPEG/WebP 编码，正常获取点阵和
 * findPic 不会产生文件 IO。
 */
bool 保存当前截图(const char* path, const 截图区域* region);

/** 清理全部模板缓存，或清理指定图片路径对应的模板缓存。 */
void 清理图片缓存(const char* picName);

/**
 * 设置当前脚本任务的模板缓存上限，单位字节。
 *
 * 设置为 0 时关闭模板缓存；缩小上限会立即按 LRU 淘汰旧模板。
 */
void 设置图片缓存上限(std::size_t maxBytes);

/** 脚本任务结束时清空全部模板，并把缓存上限恢复为默认 5 MiB。 */
void 重置图片缓存();

/** 返回当前线程最近一次图片 API 失败原因。 */
std::string 取图片错误();

} // namespace xiaoyv::api
