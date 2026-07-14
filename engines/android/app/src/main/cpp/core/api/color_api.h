/**
 * 文件用途：声明找色核心 API，负责在当前截图缓存上执行高速多点找色。
 */
#pragma once

#include <string>

namespace xiaoyv::api {

/**
 * 找色返回坐标。
 *
 * 找到颜色时 x/y 为命中坐标；未找到或失败时由调用方统一写成 -1/-1。
 */
struct 找色坐标 {
    int x = -1;
    int y = -1;
};

/**
 * 在指定点阵上找色。
 *
 * pixels 必须是紧凑 RGBA 点阵，长度至少为 width * height * 4。
 * frameId 用于复用转置缓存；同一帧多次按 1/4/5/8 方向找色时不会重复转置。
 */
bool 在点阵中多点找色(
        const unsigned char* pixels,
        int width,
        int height,
        long long frameId,
        int x1,
        int y1,
        int x2,
        int y2,
        int dir,
        int sim,
        const char* colors,
        找色坐标* point
);

/**
 * 在当前屏幕截图缓存上找色。
 *
 * 该函数内部调用 screen_api 获取当前帧，因此不需要“是否截屏”参数；是否复用缓存
 * 由 screen_api 的缓存时间和 keep/release 状态统一决定。
 */
bool 在屏幕中多点找色(
        int x1,
        int y1,
        int x2,
        int y2,
        int dir,
        int sim,
        const char* colors,
        找色坐标* point
);

/**
 * 清空找色内部缓存。
 *
 * 当前主要用于释放转置点阵缓存，避免脚本边界或截图清理后继续持有旧帧数据。
 */
void 清空找色缓存();

/**
 * 返回最近一次找色失败原因。
 */
std::string 取找色错误();

} // namespace xiaoyv::api
