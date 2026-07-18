/**
 * 文件用途：实现跨平台 RGBA 颜色解析、二值化和完整点阵生成算法。
 */
#include "image_core.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace xiaoyv::image {
namespace {

int hexValue(char character) {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}

bool parseHexColor(const std::string& text, int* red, int* green, int* blue) {
    if (red == nullptr || green == nullptr || blue == nullptr || text.size() != 6) return false;
    int values[6]{};
    for (int index = 0; index < 6; ++index) {
        values[index] = hexValue(text[static_cast<std::size_t>(index)]);
        if (values[index] < 0) return false;
    }
    *red = values[0] * 16 + values[1];
    *green = values[2] * 16 + values[3];
    *blue = values[4] * 16 + values[5];
    return true;
}

std::string trimAscii(std::string text) {
    while (!text.empty() && static_cast<unsigned char>(text.front()) <= 0x20) text.erase(text.begin());
    while (!text.empty() && static_cast<unsigned char>(text.back()) <= 0x20) text.pop_back();
    return text;
}

bool validateImage(const RgbaImageView& image, std::string* error) {
    if (image.pixels == nullptr || image.width <= 0 || image.height <= 0) {
        if (error != nullptr) *error = "RGBA 图片为空";
        return false;
    }
    long long minimumStride = static_cast<long long>(image.width) * 4LL;
    if (image.strideBytes < minimumStride) {
        if (error != nullptr) *error = "RGBA 图片行跨度不足";
        return false;
    }
    return true;
}

struct IndexBand {
    int start = 0;
    int end = 0;
};

/** 按指定最小空白长度切分一维前景投影，返回包含前景的闭区间。 */
std::vector<IndexBand> splitOccupiedBands(
        const std::vector<std::uint8_t>& occupied,
        int minimumBlank
) {
    std::vector<IndexBand> bands;
    int bandStart = -1;
    int lastOccupied = -1;
    for (int index = 0; index < static_cast<int>(occupied.size()); ++index) {
        if (occupied[static_cast<std::size_t>(index)] != 0) {
            if (bandStart < 0) bandStart = index;
            lastOccupied = index;
            continue;
        }
        if (bandStart >= 0 && index - lastOccupied >= minimumBlank) {
            bands.push_back({bandStart, lastOccupied});
            bandStart = -1;
            lastOccupied = -1;
        }
    }
    if (bandStart >= 0) bands.push_back({bandStart, lastOccupied});
    return bands;
}

} // namespace

bool parseColorRule(const std::string& rawText, ColorRule* rule, std::string* error) {
    if (rule == nullptr) {
        if (error != nullptr) *error = "颜色规则输出对象为空";
        return false;
    }
    std::string text = trimAscii(rawText);
    std::size_t separator = text.find('-');
    std::string target = separator == std::string::npos ? text : text.substr(0, separator);
    std::string delta = separator == std::string::npos ? "000000" : text.substr(separator + 1);
    if (target.rfind("0x", 0) == 0 || target.rfind("0X", 0) == 0) target.erase(0, 2);
    if (delta.rfind("0x", 0) == 0 || delta.rfind("0X", 0) == 0) delta.erase(0, 2);

    ColorRule parsed;
    if (!parseHexColor(target, &parsed.red, &parsed.green, &parsed.blue)
            || !parseHexColor(delta, &parsed.deltaRed, &parsed.deltaGreen, &parsed.deltaBlue)) {
        if (error != nullptr) *error = "颜色格式无效，应为 RRGGBB-RRGGBB";
        return false;
    }
    *rule = parsed;
    if (error != nullptr) error->clear();
    return true;
}

bool matchesColor(const std::uint8_t* rgba, const ColorRule& rule) {
    if (rgba == nullptr) return false;
    return std::abs(static_cast<int>(rgba[0]) - rule.red) <= rule.deltaRed
            && std::abs(static_cast<int>(rgba[1]) - rule.green) <= rule.deltaGreen
            && std::abs(static_cast<int>(rgba[2]) - rule.blue) <= rule.deltaBlue;
}

bool matchesAnyColor(const std::uint8_t* rgba, const std::vector<ColorRule>& rules) {
    if (rgba == nullptr || rules.empty()) return false;
    for (const ColorRule& rule : rules) {
        if (matchesColor(rgba, rule)) return true;
    }
    return false;
}

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
) {
    return makeBinaryMask(
            image, left, top, right, bottom, std::vector<ColorRule>{rule},
            mask, maskWidth, maskHeight, error);
}

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
) {
    if (mask == nullptr || maskWidth == nullptr || maskHeight == nullptr) {
        if (error != nullptr) *error = "二值化输出对象为空";
        return false;
    }
    if (rules.empty()) {
        if (error != nullptr) *error = "颜色规则不能为空";
        return false;
    }
    if (!validateImage(image, error)) return false;

    int clippedLeft = std::max(0, left);
    int clippedTop = std::max(0, top);
    int clippedRight = std::min(image.width - 1, right);
    int clippedBottom = std::min(image.height - 1, bottom);
    if (clippedLeft > clippedRight || clippedTop > clippedBottom) {
        if (error != nullptr) *error = "图片区域无效";
        return false;
    }

    int width = clippedRight - clippedLeft + 1;
    int height = clippedBottom - clippedTop + 1;
    std::vector<std::uint8_t> output(static_cast<std::size_t>(width) * height, 0);
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* sourceRow = image.pixels
                + static_cast<std::size_t>(clippedTop + y) * image.strideBytes;
        for (int x = 0; x < width; ++x) {
            const std::uint8_t* rgba = sourceRow + static_cast<std::size_t>(clippedLeft + x) * 4;
            output[static_cast<std::size_t>(y) * width + x] = matchesAnyColor(rgba, rules) ? 1 : 0;
        }
    }
    *mask = std::move(output);
    *maskWidth = width;
    *maskHeight = height;
    if (error != nullptr) error->clear();
    return true;
}

std::string encodeMaskHex(const std::uint8_t* mask, int width, int height) {
    if (mask == nullptr || width <= 0 || height <= 0) return {};
    static constexpr char kDigits[] = "0123456789ABCDEF";
    long long totalBits = static_cast<long long>(width) * height;
    std::string output;
    output.reserve(static_cast<std::size_t>((totalBits + 3) / 4));
    for (long long start = 0; start < totalBits; start += 4) {
        int value = 0;
        for (int bit = 0; bit < 4; ++bit) {
            value <<= 1;
            long long offset = start + bit;
            if (offset < totalBits && mask[static_cast<std::size_t>(offset)] != 0) value |= 1;
        }
        output.push_back(kDigits[value]);
    }
    return output;
}

bool makeFontPixel(
        const RgbaImageView& image,
        int left,
        int top,
        int right,
        int bottom,
        const ColorRule& rule,
        std::string* fontPixel,
        std::string* error
) {
    return makeFontPixel(
            image, left, top, right, bottom, std::vector<ColorRule>{rule}, fontPixel, error);
}

bool makeFontPixel(
        const RgbaImageView& image,
        int left,
        int top,
        int right,
        int bottom,
        const std::vector<ColorRule>& rules,
        std::string* fontPixel,
        std::string* error
) {
    if (fontPixel == nullptr) {
        if (error != nullptr) *error = "字形点阵输出对象为空";
        return false;
    }
    std::vector<std::uint8_t> mask;
    int width = 0;
    int height = 0;
    if (!makeBinaryMask(
            image, left, top, right, bottom, rules,
            &mask, &width, &height, error)) {
        return false;
    }

    int foregroundLeft = width;
    int foregroundTop = height;
    int foregroundRight = -1;
    int foregroundBottom = -1;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (mask[static_cast<std::size_t>(y) * width + x] == 0) continue;
            foregroundLeft = std::min(foregroundLeft, x);
            foregroundTop = std::min(foregroundTop, y);
            foregroundRight = std::max(foregroundRight, x);
            foregroundBottom = std::max(foregroundBottom, y);
        }
    }
    if (foregroundRight < foregroundLeft || foregroundBottom < foregroundTop) {
        if (error != nullptr) *error = "选定区域内没有符合颜色规则的前景点";
        return false;
    }

    int croppedWidth = foregroundRight - foregroundLeft + 1;
    int croppedHeight = foregroundBottom - foregroundTop + 1;
    std::vector<std::uint8_t> cropped(
            static_cast<std::size_t>(croppedWidth) * croppedHeight,
            0
    );
    for (int y = 0; y < croppedHeight; ++y) {
        const std::uint8_t* source = mask.data()
                + static_cast<std::size_t>(foregroundTop + y) * width + foregroundLeft;
        std::copy(source, source + croppedWidth, cropped.data() + static_cast<std::size_t>(y) * croppedWidth);
    }
    *fontPixel = std::to_string(croppedWidth) + "$"
            + std::to_string(croppedHeight) + "$"
            + encodeMaskHex(cropped.data(), croppedWidth, croppedHeight);
    if (error != nullptr) error->clear();
    return true;
}

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
) {
    return splitFontGlyphs(
            image, left, top, right, bottom, std::vector<ColorRule>{rule},
            rowGap, columnGap, glyphs, error);
}

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
) {
    if (glyphs == nullptr) {
        if (error != nullptr) *error = "字形分割输出对象为空";
        return false;
    }
    glyphs->clear();
    if (rowGap <= 0 || columnGap <= 0) {
        if (error != nullptr) *error = "分行间隔和分字间隔必须大于 0";
        return false;
    }

    std::vector<std::uint8_t> mask;
    int maskWidth = 0;
    int maskHeight = 0;
    if (!makeBinaryMask(
            image, left, top, right, bottom, rules,
            &mask, &maskWidth, &maskHeight, error)) {
        return false;
    }
    const int originLeft = std::max(0, left);
    const int originTop = std::max(0, top);

    std::vector<std::uint8_t> occupiedRows(static_cast<std::size_t>(maskHeight), 0);
    for (int y = 0; y < maskHeight; ++y) {
        const std::uint8_t* row = mask.data() + static_cast<std::size_t>(y) * maskWidth;
        occupiedRows[static_cast<std::size_t>(y)] = std::any_of(
                row, row + maskWidth, [](std::uint8_t value) { return value != 0; }) ? 1 : 0;
    }

    const std::vector<IndexBand> rows = splitOccupiedBands(occupiedRows, rowGap);
    for (const IndexBand& rowBand : rows) {
        std::vector<std::uint8_t> occupiedColumns(static_cast<std::size_t>(maskWidth), 0);
        for (int x = 0; x < maskWidth; ++x) {
            for (int y = rowBand.start; y <= rowBand.end; ++y) {
                if (mask[static_cast<std::size_t>(y) * maskWidth + x] != 0) {
                    occupiedColumns[static_cast<std::size_t>(x)] = 1;
                    break;
                }
            }
        }

        for (const IndexBand& columnBand : splitOccupiedBands(occupiedColumns, columnGap)) {
            int glyphTop = rowBand.end;
            int glyphBottom = rowBand.start;
            for (int y = rowBand.start; y <= rowBand.end; ++y) {
                for (int x = columnBand.start; x <= columnBand.end; ++x) {
                    if (mask[static_cast<std::size_t>(y) * maskWidth + x] == 0) continue;
                    glyphTop = std::min(glyphTop, y);
                    glyphBottom = std::max(glyphBottom, y);
                }
            }

            FontGlyph glyph;
            glyph.left = originLeft + columnBand.start;
            glyph.top = originTop + glyphTop;
            glyph.width = columnBand.end - columnBand.start + 1;
            glyph.height = glyphBottom - glyphTop + 1;
            glyph.mask.resize(static_cast<std::size_t>(glyph.width) * glyph.height, 0);
            for (int y = 0; y < glyph.height; ++y) {
                const std::uint8_t* source = mask.data()
                        + static_cast<std::size_t>(glyphTop + y) * maskWidth + columnBand.start;
                std::copy(source, source + glyph.width,
                          glyph.mask.data() + static_cast<std::size_t>(y) * glyph.width);
            }
            glyphs->push_back(std::move(glyph));
        }
    }

    if (glyphs->empty()) {
        if (error != nullptr) *error = "选定区域内没有可分割的前景点";
        return false;
    }
    if (error != nullptr) error->clear();
    return true;
}

std::vector<MatchPoint> findBinaryPattern(
        const std::uint8_t* imageMask,
        int imageWidth,
        int imageHeight,
        const std::uint8_t* patternMask,
        int patternWidth,
        int patternHeight,
        double similarity,
        std::size_t maximumMatches
) {
    std::vector<MatchPoint> matches;
    if (imageMask == nullptr || patternMask == nullptr
            || imageWidth <= 0 || imageHeight <= 0
            || patternWidth <= 0 || patternHeight <= 0
            || patternWidth > imageWidth || patternHeight > imageHeight
            || !std::isfinite(similarity) || similarity <= 0.0 || similarity > 1.0
            || maximumMatches == 0) {
        return matches;
    }

    int foregroundCount = 0;
    for (int index = 0; index < patternWidth * patternHeight; ++index) {
        if (patternMask[index] != 0) ++foregroundCount;
    }
    if (foregroundCount == 0) return matches;
    int allowedErrors = static_cast<int>(std::floor(
            (1.0 - similarity) * foregroundCount + 1e-9
    ));

    for (int top = 0; top + patternHeight <= imageHeight; ++top) {
        for (int left = 0; left + patternWidth <= imageWidth; ++left) {
            int missing = 0;
            int extra = 0;
            bool rejected = false;
            for (int y = 0; y < patternHeight && !rejected; ++y) {
                const std::uint8_t* imageRow = imageMask
                        + static_cast<std::size_t>(top + y) * imageWidth + left;
                const std::uint8_t* patternRow = patternMask
                        + static_cast<std::size_t>(y) * patternWidth;
                for (int x = 0; x < patternWidth; ++x) {
                    bool expected = patternRow[x] != 0;
                    bool actual = imageRow[x] != 0;
                    if (expected && !actual) ++missing;
                    if (!expected && actual) ++extra;
                    if (missing > allowedErrors || extra > allowedErrors) {
                        rejected = true;
                        break;
                    }
                }
            }
            if (!rejected) {
                matches.push_back({left, top});
                if (matches.size() >= maximumMatches) return matches;
            }
        }
    }
    return matches;
}

} // namespace xiaoyv::image
