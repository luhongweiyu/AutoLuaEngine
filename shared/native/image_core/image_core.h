/**
 * 文件用途：声明桌面工具和各平台引擎共用的 RGBA 图像、二值化和点阵生成算法。
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace xiaoyv::image {

/** 不持有内存的紧凑 RGBA8888 图像视图。 */
struct RgbaImageView {
    const std::uint8_t* pixels = nullptr;
    int width = 0;
    int height = 0;
    int strideBytes = 0;
};

/** 目标 RGB 和每个通道允许的绝对偏差。 */
struct ColorRule {
    int red = 0;
    int green = 0;
    int blue = 0;
    int deltaRed = 0;
    int deltaGreen = 0;
    int deltaBlue = 0;
};

/** 二值模板在图片中的左上角命中坐标。 */
struct MatchPoint {
    int x = -1;
    int y = -1;
};

/** 从多字图片中分割出的单个完整点阵及其原图坐标。 */
struct FontGlyph {
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> mask;
};

/** 解析 RRGGBB 或 RRGGBB-RRGGBB 颜色规则。 */
bool parseColorRule(const std::string& text, ColorRule* rule, std::string* error);

/** 判断一个 RGBA 像素是否满足颜色规则，alpha 不参与比较。 */
bool matchesColor(const std::uint8_t* rgba, const ColorRule& rule);

/** 任一颜色规则命中即视为前景。 */
bool matchesAnyColor(const std::uint8_t* rgba, const std::vector<ColorRule>& rules);

/**
 * 把指定闭区间转换成 0/1 二值掩码。
 *
 * 输出按行优先保存，每个像素占一个字节；范围会裁剪到图片边界，但不会交换无效坐标。
 * 多规则版本：像素匹配任意一条规则即为 1。
 */
bool makeBinaryMask(
        const RgbaImageView& image,
        int left,
        int top,
        int right,
        int bottom,
        const ColorRule& rule,
        std::vector<std::uint8_t>* mask,
        int* maskWidth,
        int* maskHeight,
        std::string* error
);

bool makeBinaryMask(
        const RgbaImageView& image,
        int left,
        int top,
        int right,
        int bottom,
        const std::vector<ColorRule>& rules,
        std::vector<std::uint8_t>* mask,
        int* maskWidth,
        int* maskHeight,
        std::string* error
);

/** 把完整 0/1 掩码按高位在前编码成十六进制文本。 */
std::string encodeMaskHex(const std::uint8_t* mask, int width, int height);

/**
 * 从图片区域生成“宽$高$十六进制点阵”。
 *
 * 纯背景边缘会自动裁掉。Android 的 m.font.getFontPixel 与桌面字库工具必须共同调用此函数，
 * 从而保证同一张图片、同一条颜色规则得到完全相同的点阵。
 */
bool makeFontPixel(
        const RgbaImageView& image,
        int left,
        int top,
        int right,
        int bottom,
        const ColorRule& rule,
        std::string* fontPixel,
        std::string* error
);

bool makeFontPixel(
        const RgbaImageView& image,
        int left,
        int top,
        int right,
        int bottom,
        const std::vector<ColorRule>& rules,
        std::string* fontPixel,
        std::string* error
);

/**
 * 从图片区域批量分割字形。
 *
 * 连续空白行达到 rowGap 时切分文本行；每行内连续空白列达到 columnGap 时切分字形。
 * 小于阈值的空白保留在同一个字形内，以容纳断开的笔画。结果按从上到下、从左到右排序。
 */
bool splitFontGlyphs(
        const RgbaImageView& image,
        int left,
        int top,
        int right,
        int bottom,
        const ColorRule& rule,
        int rowGap,
        int columnGap,
        std::vector<FontGlyph>* glyphs,
        std::string* error
);

bool splitFontGlyphs(
        const RgbaImageView& image,
        int left,
        int top,
        int right,
        int bottom,
        const std::vector<ColorRule>& rules,
        int rowGap,
        int columnGap,
        std::vector<FontGlyph>* glyphs,
        std::string* error
);

/**
 * 在完整二值图片中查找模板；缺失点和多余点分别受 similarity 限制。
 *
 * 该函数供桌面字库编辑器即时测试单个字形，不负责字库标签排序和文字行拼接。
 */
std::vector<MatchPoint> findBinaryPattern(
        const std::uint8_t* imageMask,
        int imageWidth,
        int imageHeight,
        const std::uint8_t* patternMask,
        int patternWidth,
        int patternHeight,
        double similarity,
        std::size_t maximumMatches = 1024
);

} // namespace xiaoyv::image
