/**
 * 文件用途：实现高速多点找色核心 API，算法整理自旧项目找色实现。
 */
#include "color_api.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

#include "screen_api.h"

namespace autolua::api {
namespace {

// 当前 screen_api 固定返回紧凑 RGBA8888 点阵，每个像素占 4 字节。
constexpr int kPixelBytes = 4;

// 保持旧项目限制：单次多点找色最多解析 500 个颜色点。
constexpr int kMaxColorItems = 500;

/**
 * 预处理后的颜色比较项。
 *
 * offset 是相对锚点的像素偏移，解析阶段已根据扫描方向决定是否按转置点阵计算。
 * 比较阶段只需要 anchorIndex + offset，不再做坐标乘加。
 */
struct ColorCompareItem {
    int offset = 0;
    int16_t rMin = 0;
    int16_t gMin = 0;
    int16_t bMin = 0;
    uint16_t rRange = 0;
    uint16_t gRange = 0;
    uint16_t bRange = 0;
};

/**
 * 解析 colors 后得到的热路径数据。
 */
struct ParsedColors {
    std::array<ColorCompareItem, kMaxColorItems> items;
    int count = 0;
    int minRelativeX = 0;
    int maxRelativeX = 0;
    int minRelativeY = 0;
    int maxRelativeY = 0;
};

std::mutex gColorMutex;
std::vector<unsigned char> gTransposedPixels;
int gTransposedWidth = 0;
int gTransposedHeight = 0;
long long gTransposedRevision = -1;
std::string gLastError;

int hexCharToValue(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

bool parseSignedInt(const char*& cursor, int& result) {
    const char* start = cursor;
    bool negative = false;

    if (*cursor == '-' || *cursor == '+') {
        negative = (*cursor == '-');
        ++cursor;
    }

    if (*cursor < '0' || *cursor > '9') {
        cursor = start;
        return false;
    }

    long long value = 0;
    while (*cursor >= '0' && *cursor <= '9') {
        value = value * 10 + (*cursor - '0');
        long long limit = negative
                ? static_cast<long long>(std::numeric_limits<int>::max()) + 1
                : static_cast<long long>(std::numeric_limits<int>::max());
        if (value > limit) {
            cursor = start;
            return false;
        }
        ++cursor;
    }

    result = negative ? static_cast<int>(-value) : static_cast<int>(value);
    return true;
}

bool parseHex6(const char*& cursor, int& result) {
    const char* start = cursor;
    if (cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X')) {
        cursor += 2;
    }

    int value = 0;
    for (int index = 0; index < 6; ++index) {
        int halfByte = hexCharToValue(*cursor);
        if (halfByte < 0) {
            cursor = start;
            return false;
        }
        value = (value << 4) | halfByte;
        ++cursor;
    }

    result = value;
    return true;
}

bool parseColorPiece(
        const char*& cursor,
        int& x,
        int& y,
        int& color,
        bool& hasOwnTolerance,
        int& tolerance
) {
    const char* start = cursor;

    if (!parseSignedInt(cursor, x) || *cursor != '|') {
        cursor = start;
        return false;
    }
    ++cursor;

    if (!parseSignedInt(cursor, y) || *cursor != '|') {
        cursor = start;
        return false;
    }
    ++cursor;

    if (!parseHex6(cursor, color)) {
        cursor = start;
        return false;
    }

    hasOwnTolerance = false;
    if (*cursor == '-' || *cursor == '|') {
        ++cursor;
        if (!parseHex6(cursor, tolerance)) {
            cursor = start;
            return false;
        }
        hasOwnTolerance = true;
    }

    return true;
}

bool setErrorLocked(const std::string& error) {
    gLastError = error;
    return false;
}

void writeNotFoundPoint(ColorPoint* point) {
    if (point != nullptr) {
        point->x = -1;
        point->y = -1;
    }
}

bool isTransposedDirection(int dir) {
    return dir == 1 || dir == 4 || dir == 5 || dir == 8;
}

bool parseColorsLocked(
        const char* colors,
        int width,
        int height,
        bool useTransposedPixels,
        int sim,
        ParsedColors* parsed
) {
    if (colors == nullptr || parsed == nullptr) {
        return setErrorLocked("找色参数为空");
    }
    if (sim < 0 || sim > 0xFFFFFF) {
        return setErrorLocked("找色容差必须在 0x000000 到 0xFFFFFF 之间");
    }

    int anchorX = 0;
    int anchorY = 0;
    bool hasAnchor = false;
    const char* cursor = colors;

    while (*cursor != '\0') {
        int pointX = 0;
        int pointY = 0;
        int color = 0;
        int tolerance = sim;
        bool hasOwnTolerance = false;
        const char* pieceStart = cursor;

        if (!parseColorPiece(cursor, pointX, pointY, color, hasOwnTolerance, tolerance)) {
            cursor = pieceStart + 1;
            continue;
        }

        if (!hasOwnTolerance) {
            tolerance = sim;
        }
        if (tolerance < 0 || tolerance > 0xFFFFFF) {
            return setErrorLocked("找色独立容差必须在 0x000000 到 0xFFFFFF 之间");
        }

        if (!hasAnchor) {
            anchorX = pointX;
            anchorY = pointY;
            hasAnchor = true;
        }

        if (parsed->count >= kMaxColorItems) {
            return setErrorLocked("找色颜色点数量超过 500");
        }

        int relativeX = pointX - anchorX;
        int relativeY = pointY - anchorY;

        ColorCompareItem item;
        item.offset = useTransposedPixels
                ? relativeX * height + relativeY
                : relativeY * width + relativeX;

        int targetR = (color >> 16) & 0xff;
        int targetG = (color >> 8) & 0xff;
        int targetB = color & 0xff;
        int toleranceR = (tolerance >> 16) & 0xff;
        int toleranceG = (tolerance >> 8) & 0xff;
        int toleranceB = tolerance & 0xff;

        item.rMin = static_cast<int16_t>(targetR - toleranceR);
        item.gMin = static_cast<int16_t>(targetG - toleranceG);
        item.bMin = static_cast<int16_t>(targetB - toleranceB);
        item.rRange = static_cast<uint16_t>(toleranceR * 2);
        item.gRange = static_cast<uint16_t>(toleranceG * 2);
        item.bRange = static_cast<uint16_t>(toleranceB * 2);

        if (parsed->count == 0) {
            parsed->minRelativeX = relativeX;
            parsed->maxRelativeX = relativeX;
            parsed->minRelativeY = relativeY;
            parsed->maxRelativeY = relativeY;
        } else {
            parsed->minRelativeX = std::min(parsed->minRelativeX, relativeX);
            parsed->maxRelativeX = std::max(parsed->maxRelativeX, relativeX);
            parsed->minRelativeY = std::min(parsed->minRelativeY, relativeY);
            parsed->maxRelativeY = std::max(parsed->maxRelativeY, relativeY);
        }

        parsed->items[parsed->count] = item;
        ++parsed->count;
    }

    if (!hasAnchor || parsed->count <= 0) {
        return setErrorLocked("找色颜色字符串没有有效颜色点");
    }

    if (parsed->count > 1) {
        std::sort(
                parsed->items.begin(),
                parsed->items.begin() + parsed->count,
                [](const ColorCompareItem& left, const ColorCompareItem& right) {
                    unsigned int leftScore = static_cast<unsigned int>(left.rRange)
                            + static_cast<unsigned int>(left.gRange)
                            + static_cast<unsigned int>(left.bRange);
                    unsigned int rightScore = static_cast<unsigned int>(right.rRange)
                            + static_cast<unsigned int>(right.gRange)
                            + static_cast<unsigned int>(right.bRange);
                    return leftScore < rightScore;
                }
        );
    }

    return true;
}

bool normalizeRangeLocked(
        int width,
        int height,
        const ParsedColors& parsed,
        int& x1,
        int& y1,
        int& x2,
        int& y2
) {
    if ((0 - parsed.minRelativeX) > x1) {
        x1 = 0 - parsed.minRelativeX;
    }
    if ((0 - parsed.minRelativeY) > y1) {
        y1 = 0 - parsed.minRelativeY;
    }
    if ((x2 + parsed.maxRelativeX) > (width - 1)) {
        x2 = width - 1 - parsed.maxRelativeX;
    }
    if ((y2 + parsed.maxRelativeY) > (height - 1)) {
        y2 = height - 1 - parsed.maxRelativeY;
    }

    if (x1 > x2 || y1 > y2) {
        return setErrorLocked("找色范围为空");
    }

    return true;
}

bool compareColorItems(
        const unsigned char* pixels,
        const ParsedColors& parsed,
        int anchorIndex
) {
    for (int index = 0; index < parsed.count; ++index) {
        const ColorCompareItem& item = parsed.items[index];
        const unsigned char* pixel = pixels + (anchorIndex + item.offset) * kPixelBytes;

        if ((unsigned) ((int) pixel[0] - (int) item.rMin) > (unsigned) item.rRange) {
            return false;
        }
        if ((unsigned) ((int) pixel[1] - (int) item.gMin) > (unsigned) item.gRange) {
            return false;
        }
        if ((unsigned) ((int) pixel[2] - (int) item.bMin) > (unsigned) item.bRange) {
            return false;
        }
    }
    return true;
}

bool rebuildTransposedPixelsLocked(
        const unsigned char* pixels,
        int width,
        int height,
        long long frameRevision
) {
    if (gTransposedRevision == frameRevision
            && gTransposedWidth == width
            && gTransposedHeight == height
            && !gTransposedPixels.empty()) {
        return true;
    }

    size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    gTransposedPixels.resize(pixelCount * kPixelBytes);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t source = (static_cast<size_t>(y) * width + x) * kPixelBytes;
            size_t target = (static_cast<size_t>(x) * height + y) * kPixelBytes;
            gTransposedPixels[target] = pixels[source];
            gTransposedPixels[target + 1] = pixels[source + 1];
            gTransposedPixels[target + 2] = pixels[source + 2];
            gTransposedPixels[target + 3] = pixels[source + 3];
        }
    }

    gTransposedWidth = width;
    gTransposedHeight = height;
    gTransposedRevision = frameRevision;
    return true;
}

bool scanParsedColorsLocked(
        const unsigned char* pixels,
        int width,
        int height,
        int x1,
        int y1,
        int x2,
        int y2,
        int dir,
        const ParsedColors& parsed,
        ColorPoint* point
) {
    switch (dir) {
        case 1:
            for (int x = x1; x <= x2; ++x) {
                int anchorIndex = x * height + y1;
                for (int y = y1; y <= y2; ++y) {
                    if (compareColorItems(pixels, parsed, anchorIndex)) {
                        point->x = x;
                        point->y = y;
                        return true;
                    }
                    ++anchorIndex;
                }
            }
            break;
        case 2:
            for (int y = y1; y <= y2; ++y) {
                int anchorIndex = y * width + x1;
                for (int x = x1; x <= x2; ++x) {
                    if (compareColorItems(pixels, parsed, anchorIndex)) {
                        point->x = x;
                        point->y = y;
                        return true;
                    }
                    ++anchorIndex;
                }
            }
            break;
        case 3:
            for (int y = y1; y <= y2; ++y) {
                int anchorIndex = y * width + x2;
                for (int x = x2; x >= x1; --x) {
                    if (compareColorItems(pixels, parsed, anchorIndex)) {
                        point->x = x;
                        point->y = y;
                        return true;
                    }
                    --anchorIndex;
                }
            }
            break;
        case 4:
            for (int x = x2; x >= x1; --x) {
                int anchorIndex = x * height + y1;
                for (int y = y1; y <= y2; ++y) {
                    if (compareColorItems(pixels, parsed, anchorIndex)) {
                        point->x = x;
                        point->y = y;
                        return true;
                    }
                    ++anchorIndex;
                }
            }
            break;
        case 5:
            for (int x = x2; x >= x1; --x) {
                int anchorIndex = x * height + y2;
                for (int y = y2; y >= y1; --y) {
                    if (compareColorItems(pixels, parsed, anchorIndex)) {
                        point->x = x;
                        point->y = y;
                        return true;
                    }
                    --anchorIndex;
                }
            }
            break;
        case 6:
            for (int y = y2; y >= y1; --y) {
                int anchorIndex = y * width + x2;
                for (int x = x2; x >= x1; --x) {
                    if (compareColorItems(pixels, parsed, anchorIndex)) {
                        point->x = x;
                        point->y = y;
                        return true;
                    }
                    --anchorIndex;
                }
            }
            break;
        case 7:
            for (int y = y2; y >= y1; --y) {
                int anchorIndex = y * width + x1;
                for (int x = x1; x <= x2; ++x) {
                    if (compareColorItems(pixels, parsed, anchorIndex)) {
                        point->x = x;
                        point->y = y;
                        return true;
                    }
                    ++anchorIndex;
                }
            }
            break;
        case 8:
            for (int x = x1; x <= x2; ++x) {
                int anchorIndex = x * height + y2;
                for (int y = y2; y >= y1; --y) {
                    if (compareColorItems(pixels, parsed, anchorIndex)) {
                        point->x = x;
                        point->y = y;
                        return true;
                    }
                    --anchorIndex;
                }
            }
            break;
        default:
            return setErrorLocked("找色方向必须是 1 到 8");
    }

    return setErrorLocked("未找到匹配颜色");
}

} // namespace

bool findColorOnFrame(
        const unsigned char* pixels,
        int width,
        int height,
        long long frameRevision,
        int x1,
        int y1,
        int x2,
        int y2,
        int dir,
        int sim,
        const char* colors,
        ColorPoint* point
) {
    std::lock_guard<std::mutex> lock(gColorMutex);
    writeNotFoundPoint(point);

    if (point == nullptr) {
        return setErrorLocked("找色输出坐标为空");
    }
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return setErrorLocked("找色点阵为空");
    }

    bool useTransposedPixels = isTransposedDirection(dir);
    ParsedColors parsed;
    if (!parseColorsLocked(colors, width, height, useTransposedPixels, sim, &parsed)) {
        return false;
    }
    if (!normalizeRangeLocked(width, height, parsed, x1, y1, x2, y2)) {
        return false;
    }

    const unsigned char* searchPixels = pixels;
    if (useTransposedPixels) {
        if (!rebuildTransposedPixelsLocked(pixels, width, height, frameRevision)) {
            return false;
        }
        searchPixels = gTransposedPixels.data();
    }

    bool found = scanParsedColorsLocked(
            searchPixels,
            width,
            height,
            x1,
            y1,
            x2,
            y2,
            dir,
            parsed,
            point
    );
    if (found) {
        gLastError.clear();
    }
    return found;
}

bool findColorOnScreen(
        int x1,
        int y1,
        int x2,
        int y2,
        int dir,
        int sim,
        const char* colors,
        ColorPoint* point
) {
    writeNotFoundPoint(point);

    ScreenFrame frame;
    if (!captureScreen(&frame)) {
        std::lock_guard<std::mutex> lock(gColorMutex);
        return setErrorLocked(screenLastError());
    }

    return findColorOnFrame(
            frame.pixels,
            frame.width,
            frame.height,
            frame.revision,
            x1,
            y1,
            x2,
            y2,
            dir,
            sim,
            colors,
            point
    );
}

void clearColorCache() {
    std::lock_guard<std::mutex> lock(gColorMutex);
    gTransposedPixels.clear();
    gTransposedPixels.shrink_to_fit();
    gTransposedWidth = 0;
    gTransposedHeight = 0;
    gTransposedRevision = -1;
    gLastError.clear();
}

std::string colorLastError() {
    std::lock_guard<std::mutex> lock(gColorMutex);
    return gLastError;
}

} // namespace autolua::api
