/**
 * 文件用途：声明桌面端图片处理入口，集中完成 QImage 与跨平台 image_core 的数据适配。
 */
#pragma once

#include <QImage>
#include <QRect>
#include <QString>

#include <cstdint>
#include <vector>

#include "native/image_core/image_core.h"

namespace xiaoyv::tools {

enum class BinaryMode {
    Color,
    Grayscale,
};

/** 二值化参数是一个完整值对象，可直接比较是否仍为当前预览参数。 */
struct BinarySettings {
    BinaryMode mode = BinaryMode::Color;
    QString colorRules = QStringLiteral("FFFFFF-000000");
    int grayscaleThreshold = 128;
    bool inverted = false;

    bool operator==(const BinarySettings& other) const = default;
};

/** 点阵提取结果；第一项标记为未分割总览，后续项始终保留自动分割字形。 */
struct ExtractedGlyph {
    QRect bounds;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> mask;
    bool overview = false;

    QString pixelBody() const;
};

/** 转为紧凑 RGBA8888；输入已经符合时仍利用 QImage 隐式共享。 */
QImage toRgba8888(const QImage& image);

/** 根据完整设置生成黑白预览图，不修改输入图片。 */
bool makeBinaryPreview(
        const QImage& source,
        const BinarySettings& settings,
        QImage* output,
        QString* error = nullptr);

/** 解析“RRGGBB-偏色|RRGGBB-偏色”规则。 */
bool parseColorRules(
        const QString& text,
        std::vector<xiaoyv::image::ColorRule>* rules,
        QString* error = nullptr);

/** 把区域按颜色规则转为 0/1 掩码，范围采用闭区间。 */
bool makeColorMask(
        const QImage& source,
        const QRect& requestedRange,
        const std::vector<xiaoyv::image::ColorRule>& rules,
        std::vector<std::uint8_t>* mask,
        int* width,
        int* height,
        QRect* actualRange,
        QString* error = nullptr);

/**
 * 提取完整区域和分割字形。
 *
 * 未分割结果固定排第一；后续逐项追加分割结果，即使只有一个字形且点阵相同也不去重。
 */
bool extractFontGlyphs(
        const QImage& source,
        const QRect& requestedRange,
        const std::vector<xiaoyv::image::ColorRule>& rules,
        int rowGap,
        int columnGap,
        std::vector<ExtractedGlyph>* glyphs,
        QString* error = nullptr);

/** 解析“宽$高$十六进制”或“文字$宽$高$十六进制”。 */
bool decodeFontPixel(
        const QString& text,
        int* width,
        int* height,
        std::vector<std::uint8_t>* mask,
        QString* label = nullptr,
        QString* error = nullptr);

/** 把点阵编码为“宽$高$十六进制”。 */
QString encodeFontPixel(int width, int height, const std::vector<std::uint8_t>& mask);

} // namespace xiaoyv::tools
