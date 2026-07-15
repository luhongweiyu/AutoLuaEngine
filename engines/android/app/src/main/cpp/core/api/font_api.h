/**
 * 文件用途：声明自定义点阵字库识字和找字核心 API，兼容大漠/懒人旧字库并支持任意尺寸手机字库。
 */
#pragma once

#include <string>

namespace xiaoyv::api {

/** 找字命中坐标。未找到或失败时 x/y 为 -1。 */
struct 字库坐标 {
    int x = -1;
    int y = -1;
};

/** 替换指定索引的字库。dictionary 可为文本、普通文件路径或当前 ALPKG 包内资源路径。 */
bool 设置字库(int index, const char* dictionary);

/** 向指定索引追加字库内容。 */
bool 追加字库(int index, const char* dictionary);

/** 选择当前线程要使用的字库索引。 */
bool 使用字库(int index);

/**
 * 从当前截图生成一个可写入新字库的点阵描述，返回 "宽$高$十六进制点阵"。
 *
 * 输出会自动裁去纯背景边缘，手机字库因此不再受旧插件 11 行高度限制。
 */
bool 获取字形点阵(
        int x1,
        int y1,
        int x2,
        int y2,
        const char* color,
        std::string* fontPixel
);

/** 在当前截图指定区域内按当前字库识字，成功返回结构化 JSON。 */
bool 点阵识字(
        int x1,
        int y1,
        int x2,
        int y2,
        const char* color,
        double similarity,
        std::string* resultJson
);

/** 在当前截图指定区域内查找一段文字，返回第一个命中坐标。 */
bool 点阵找字(
        int x1,
        int y1,
        int x2,
        int y2,
        const char* text,
        const char* color,
        double similarity,
        字库坐标* point
);

/** 在当前截图指定区域内查找所有文字命中，成功返回结构化 JSON 数组。 */
bool 点阵找字全部(
        int x1,
        int y1,
        int x2,
        int y2,
        const char* text,
        const char* color,
        double similarity,
        std::string* resultJson
);

/** 返回当前线程最近一次字库 API 失败原因。 */
std::string 取字库错误();

} // namespace xiaoyv::api
