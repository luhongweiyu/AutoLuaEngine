/**
 * 文件用途：实现 QImage 二值化、颜色规则、完整点阵和分割点阵的统一适配。
 */
#include "core/image_processing.h"

#include "core/selection_range.h"

#include <QRegularExpression>

#include <algorithm>
#include <string>
#include <utility>

namespace xiaoyv::tools {
namespace {

xiaoyv::image::RgbaImageView imageView(const QImage& rgba) {
    return {
            reinterpret_cast<const std::uint8_t*>(rgba.constBits()),
            rgba.width(),
            rgba.height(),
            static_cast<int>(rgba.bytesPerLine()),
    };
}

/** 从完整二值掩码中裁掉四周纯背景，内部空白保持不变。 */
bool makeOverviewGlyph(
        const std::vector<std::uint8_t>& source,
        int sourceWidth,
        int sourceHeight,
        const QPoint& origin,
        ExtractedGlyph* glyph,
        QString* error) {
    int left = sourceWidth;
    int top = sourceHeight;
    int right = -1;
    int bottom = -1;
    for (int y = 0; y < sourceHeight; ++y) {
        for (int x = 0; x < sourceWidth; ++x) {
            if (source[static_cast<std::size_t>(y) * sourceWidth + x] == 0) continue;
            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x);
            bottom = std::max(bottom, y);
        }
    }
    if (right < left || bottom < top) {
        if (error != nullptr) *error = QString::fromUtf8("选定区域内没有符合颜色规则的前景点");
        return false;
    }

    ExtractedGlyph result;
    result.bounds = QRect(
            origin + QPoint(left, top),
            origin + QPoint(right, bottom));
    result.width = result.bounds.width();
    result.height = result.bounds.height();
    result.overview = true;
    result.mask.resize(static_cast<std::size_t>(result.width) * result.height, 0);
    for (int y = 0; y < result.height; ++y) {
        const std::uint8_t* row = source.data()
                + static_cast<std::size_t>(top + y) * sourceWidth + left;
        std::copy(row, row + result.width,
                  result.mask.data() + static_cast<std::size_t>(y) * result.width);
    }
    *glyph = std::move(result);
    return true;
}

} // namespace

QString ExtractedGlyph::pixelBody() const {
    return encodeFontPixel(width, height, mask);
}

QImage toRgba8888(const QImage& image) {
    if (image.isNull()) return {};
    if (image.format() == QImage::Format_RGBA8888) return image;
    return image.convertToFormat(QImage::Format_RGBA8888);
}

bool makeBinaryPreview(
        const QImage& source,
        const BinarySettings& settings,
        QImage* output,
        QString* error) {
    if (output == nullptr) {
        if (error != nullptr) *error = QString::fromUtf8("二值化输出对象为空");
        return false;
    }
    const QImage rgba = toRgba8888(source);
    if (rgba.isNull()) {
        if (error != nullptr) *error = QString::fromUtf8("当前图片为空");
        return false;
    }
    if (settings.grayscaleThreshold < 0 || settings.grayscaleThreshold > 255) {
        if (error != nullptr) *error = QString::fromUtf8("灰度阈值必须在 0 到 255 之间");
        return false;
    }
    std::vector<xiaoyv::image::ColorRule> colorRules;
    if (settings.mode == BinaryMode::Color
            && !parseColorRules(settings.colorRules, &colorRules, error)) {
        return false;
    }

    QImage result(rgba.size(), QImage::Format_RGBA8888);
    for (int y = 0; y < rgba.height(); ++y) {
        const uchar* sourceRow = rgba.constScanLine(y);
        uchar* targetRow = result.scanLine(y);
        for (int x = 0; x < rgba.width(); ++x) {
            const uchar* pixel = sourceRow + x * 4;
            bool foreground = false;
            if (settings.mode == BinaryMode::Color) {
                foreground = xiaoyv::image::matchesAnyColor(pixel, colorRules);
            } else {
                const int gray = (static_cast<int>(pixel[0]) * 299
                        + static_cast<int>(pixel[1]) * 587
                        + static_cast<int>(pixel[2]) * 114) / 1000;
                foreground = gray >= settings.grayscaleThreshold;
            }
            if (settings.inverted) foreground = !foreground;
            const uchar value = foreground ? 255 : 0;
            targetRow[x * 4] = value;
            targetRow[x * 4 + 1] = value;
            targetRow[x * 4 + 2] = value;
            targetRow[x * 4 + 3] = 255;
        }
    }
    *output = std::move(result);
    if (error != nullptr) error->clear();
    return true;
}

bool parseColorRules(
        const QString& text,
        std::vector<xiaoyv::image::ColorRule>* rules,
        QString* error) {
    if (rules == nullptr) {
        if (error != nullptr) *error = QString::fromUtf8("颜色规则输出对象为空");
        return false;
    }
    rules->clear();
    const QStringList parts = text.split(QLatin1Char('|'), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        xiaoyv::image::ColorRule rule;
        std::string nativeError;
        if (!xiaoyv::image::parseColorRule(part.trimmed().toStdString(), &rule, &nativeError)) {
            if (error != nullptr) {
                *error = QString::fromUtf8("颜色规则无效：%1").arg(part.trimmed());
            }
            rules->clear();
            return false;
        }
        rules->push_back(rule);
    }
    if (rules->empty()) {
        if (error != nullptr) {
            *error = QString::fromUtf8("请填写颜色规则，例如 D9F9FF-505050");
        }
        return false;
    }
    if (error != nullptr) error->clear();
    return true;
}

bool makeColorMask(
        const QImage& source,
        const QRect& requestedRange,
        const std::vector<xiaoyv::image::ColorRule>& rules,
        std::vector<std::uint8_t>* mask,
        int* width,
        int* height,
        QRect* actualRange,
        QString* error) {
    const QImage rgba = toRgba8888(source);
    if (rgba.isNull()) {
        if (error != nullptr) *error = QString::fromUtf8("当前图片为空");
        return false;
    }
    const QRect range = effectiveSelectionRange(requestedRange, rgba.rect());
    if (range.isEmpty()) {
        if (error != nullptr) *error = QString::fromUtf8("取样范围与图片没有交集");
        return false;
    }
    std::string nativeError;
    if (!xiaoyv::image::makeBinaryMask(
            imageView(rgba),
            range.left(), range.top(), range.right(), range.bottom(),
            rules, mask, width, height, &nativeError)) {
        if (error != nullptr) *error = QString::fromStdString(nativeError);
        return false;
    }
    if (actualRange != nullptr) *actualRange = range;
    if (error != nullptr) error->clear();
    return true;
}

bool extractFontGlyphs(
        const QImage& source,
        const QRect& requestedRange,
        const std::vector<xiaoyv::image::ColorRule>& rules,
        int rowGap,
        int columnGap,
        std::vector<ExtractedGlyph>* glyphs,
        QString* error) {
    if (glyphs == nullptr) {
        if (error != nullptr) *error = QString::fromUtf8("点阵结果输出对象为空");
        return false;
    }
    glyphs->clear();
    const QImage rgba = toRgba8888(source);
    std::vector<std::uint8_t> wholeMask;
    int wholeWidth = 0;
    int wholeHeight = 0;
    QRect actualRange;
    if (!makeColorMask(
            rgba, requestedRange, rules,
            &wholeMask, &wholeWidth, &wholeHeight, &actualRange, error)) {
        return false;
    }

    ExtractedGlyph overview;
    if (!makeOverviewGlyph(
            wholeMask, wholeWidth, wholeHeight,
            actualRange.topLeft(), &overview, error)) {
        return false;
    }
    glyphs->push_back(overview);

    std::vector<xiaoyv::image::FontGlyph> split;
    std::string nativeError;
    if (!xiaoyv::image::splitFontGlyphs(
            imageView(rgba),
            actualRange.left(), actualRange.top(), actualRange.right(), actualRange.bottom(),
            rules, rowGap, columnGap, &split, &nativeError)) {
        if (error != nullptr) *error = QString::fromStdString(nativeError);
        glyphs->clear();
        return false;
    }
    for (xiaoyv::image::FontGlyph& item : split) {
        ExtractedGlyph glyph;
        glyph.bounds = QRect(item.left, item.top, item.width, item.height);
        glyph.width = item.width;
        glyph.height = item.height;
        glyph.mask = std::move(item.mask);
        glyphs->push_back(std::move(glyph));
    }
    if (error != nullptr) error->clear();
    return true;
}

bool decodeFontPixel(
        const QString& text,
        int* width,
        int* height,
        std::vector<std::uint8_t>* mask,
        QString* label,
        QString* error) {
    if (width == nullptr || height == nullptr || mask == nullptr) {
        if (error != nullptr) *error = QString::fromUtf8("点阵输出对象为空");
        return false;
    }
    const QStringList parts = text.trimmed().split(QLatin1Char('$'));
    if (parts.size() != 3 && parts.size() != 4) {
        if (error != nullptr) *error = QString::fromUtf8("点阵格式应为 宽$高$十六进制");
        return false;
    }
    const int offset = parts.size() == 4 ? 1 : 0;
    bool widthOk = false;
    bool heightOk = false;
    const int parsedWidth = parts[offset].toInt(&widthOk);
    const int parsedHeight = parts[offset + 1].toInt(&heightOk);
    const QByteArray hex = parts[offset + 2].trimmed().toLatin1().toUpper();
    constexpr int kMaximumDimension = 4096;
    constexpr qint64 kMaximumPixels = 16LL * 1024 * 1024;
    const qint64 bitCount = static_cast<qint64>(parsedWidth) * parsedHeight;
    if (!widthOk || !heightOk || parsedWidth <= 0 || parsedHeight <= 0
            || parsedWidth > kMaximumDimension || parsedHeight > kMaximumDimension
            || bitCount > kMaximumPixels) {
        if (error != nullptr) *error = QString::fromUtf8("点阵宽高无效或过大");
        return false;
    }
    if (hex.size() != (bitCount + 3) / 4
            || !QRegularExpression(QStringLiteral("^[0-9A-F]+$")).match(QString::fromLatin1(hex)).hasMatch()) {
        if (error != nullptr) *error = QString::fromUtf8("点阵十六进制长度或字符无效");
        return false;
    }

    std::vector<std::uint8_t> parsed(static_cast<std::size_t>(bitCount), 0);
    for (qint64 bit = 0; bit < bitCount; ++bit) {
        const char character = hex[static_cast<int>(bit / 4)];
        const int value = character <= '9' ? character - '0' : character - 'A' + 10;
        parsed[static_cast<std::size_t>(bit)] =
                (value >> (3 - static_cast<int>(bit % 4))) & 1;
    }
    *width = parsedWidth;
    *height = parsedHeight;
    *mask = std::move(parsed);
    if (label != nullptr) *label = offset == 1 ? parts[0].trimmed() : QString{};
    if (error != nullptr) error->clear();
    return true;
}

QString encodeFontPixel(int width, int height, const std::vector<std::uint8_t>& mask) {
    if (width <= 0 || height <= 0
            || mask.size() < static_cast<std::size_t>(width) * height) {
        return {};
    }
    return QStringLiteral("%1$%2$%3")
            .arg(width)
            .arg(height)
            .arg(QString::fromStdString(
                    xiaoyv::image::encodeMaskHex(mask.data(), width, height)));
}

} // namespace xiaoyv::tools
