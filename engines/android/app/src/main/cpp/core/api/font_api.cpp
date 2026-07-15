/**
 * 文件用途：实现自定义点阵字库识字和找字，字库采用可变宽高格式适配手机高分辨率界面。
 */
#include "font_api.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "package_api.h"
#include "runtime_api.h"
#include "screen_api.h"
#include "../../engine/json_value.h"

namespace xiaoyv::api {
namespace {

constexpr int kMaxDictionaryIndex = 255;
constexpr int kMaxGlyphWidth = 256;
constexpr int kMaxGlyphHeight = 256;
constexpr int kLegacyGlyphHeight = 11;
constexpr int kRgbaPixelBytes = 4;
constexpr size_t kMaxGlyphAnchorCount = 32;
constexpr size_t kMaxFontMatches = 8192;

/**
 * 一条可变尺寸字形记录。
 *
 * bits 使用行优先 0/1 点阵；foregroundOffsets 和 anchors 在加载字库时预处理，识字热路径
 * 不再重复扫描字库十六进制文本或整张字形背景。
 */
struct 字形 {
    std::string label;
    int width = 0;
    int height = 0;
    int foregroundCount = 0;
    std::vector<unsigned char> bits;
    std::vector<int> foregroundOffsets;
    std::vector<int> anchors;
};

/** 一个字库索引下的全部字形。 */
struct 字库 {
    std::vector<字形> glyphs;
};

/** 二值化后的截图区域，所有坐标都相对于原始屏幕。 */
struct 二值区域 {
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
    std::vector<unsigned char> bits;
    std::vector<int> integral;
    std::vector<int> foregroundOffsets;
};

/** 已匹配的字形与坐标。 */
struct 识字项 {
    std::string text;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    double score = 0.0;
};

struct 颜色规则 {
    int r = 0;
    int g = 0;
    int b = 0;
    int dr = 0;
    int dg = 0;
    int db = 0;
};

std::mutex gDictionaryMutex;
std::map<int, std::shared_ptr<字库>> gDictionaries;
thread_local int gActiveDictionaryIndex = 0;
thread_local std::string gFontLastError;

bool 设置字库错误(const std::string& error) {
    gFontLastError = error;
    return false;
}

std::string 去空白(std::string value) {
    size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool 校验字库索引(int index) {
    if (index < 0 || index > kMaxDictionaryIndex) {
        return 设置字库错误("字库索引必须在 0 到 255 之间");
    }
    return true;
}

int 十六进制值(char character) {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}

/** 按 $ 切分字库行；字形文本不允许包含分隔符。 */
std::vector<std::string> 分割字库行(const std::string& line) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= line.size()) {
        size_t split = line.find('$', start);
        if (split == std::string::npos) {
            parts.push_back(line.substr(start));
            break;
        }
        parts.push_back(line.substr(start, split - start));
        start = split + 1;
    }
    return parts;
}

bool 解析正整数(const std::string& text, int* value) {
    if (value == nullptr || text.empty()) return false;
    long long number = 0;
    for (char character : text) {
        if (character < '0' || character > '9') return false;
        number = number * 10 + (character - '0');
        if (number > 1000000) return false;
    }
    *value = static_cast<int>(number);
    return true;
}

/** 解析十六进制点阵。尾部不足 4 位的点按 0 填充，实际只读取 width*height 位。 */
bool 解析点阵(const std::string& source, int width, int height, std::vector<unsigned char>* bits) {
    if (bits == nullptr || width <= 0 || height <= 0) return false;
    std::string hex;
    hex.reserve(source.size());
    for (char character : source) {
        if (character == ' ' || character == '\t' || character == ',') continue;
        if (十六进制值(character) < 0) return false;
        hex.push_back(character);
    }
    long long neededBits = static_cast<long long>(width) * static_cast<long long>(height);
    if (neededBits <= 0 || static_cast<long long>(hex.size()) * 4LL < neededBits) return false;

    bits->assign(static_cast<size_t>(neededBits), 0);
    for (long long index = 0; index < neededBits; ++index) {
        int value = 十六进制值(hex[static_cast<size_t>(index / 4)]);
        (*bits)[static_cast<size_t>(index)] = static_cast<unsigned char>((value >> (3 - (index % 4))) & 1);
    }
    return true;
}

/**
 * 解析单条字库记录。
 *
 * 新格式：字$宽$高$十六进制点阵，支持 1 到 256 任意宽高。
 * 简化旧格式：字$十六进制点阵，仍按大漠类旧字库的 11 行高度推断宽度。
 * 大漠/懒人旧格式：字$十六进制点阵$元数据$元数据$高度。中间元数据不参与点阵匹配，
 * 最后一个整数是字高。点阵按十六进制补齐到 4 位边界，所以宽度必须使用向下取整推断，
 * 不能错误要求 "十六进制位数 * 4" 恰好整除高度。
 */
bool 解析字库记录(const std::string& rawLine, 字形* glyph, std::string* error) {
    if (glyph == nullptr || error == nullptr) return false;
    std::string line = 去空白(rawLine);
    if (line.empty() || line[0] == '#') return false;
    std::vector<std::string> parts = 分割字库行(line);
    if (parts.size() < 2) {
        *error = "字库行至少应包含 字$点阵";
        return false;
    }
    std::string label = parts[0];
    if (label.empty()) {
        *error = "字库字符不能为空";
        return false;
    }

    int width = 0;
    int height = 0;
    std::string pixelText;
    // 只有第 2、3 段都是整数时才是新格式。旧字库的第 2 段是十六进制点阵，不能仅因
    // 一条记录正好有四个 $ 分段就把它误判为新格式。
    if (parts.size() == 4
            && 解析正整数(parts[1], &width)
            && 解析正整数(parts[2], &height)) {
        pixelText = parts[3];
    } else {
        pixelText = parts[1];
        std::string compact;
        for (char character : pixelText) {
            if (character != ' ' && character != '\t' && character != ',') compact.push_back(character);
        }
        long long bits = static_cast<long long>(compact.size()) * 4LL;
        if (bits == 0) {
            *error = "旧字库点阵不能为空";
            return false;
        }

        // 常见大漠/懒人字库最后一段保存真实字高，例如
        // "脚$...$1$0.0.73$16"。没有该元数据的简化旧格式才使用固定 11 行。
        height = kLegacyGlyphHeight;
        if (parts.size() >= 3 && 解析正整数(parts.back(), &height)) {
            // 已读取标准旧字库的高度，不需要额外处理元数据内容。
        }
        width = static_cast<int>(bits / height);
        if (width <= 0) {
            *error = "旧字库点阵长度不足以推断字形宽度";
            return false;
        }
    }
    if (width <= 0 || height <= 0 || width > kMaxGlyphWidth || height > kMaxGlyphHeight) {
        *error = "字库字形宽高必须在 1 到 256 之间";
        return false;
    }

    字形 parsed;
    parsed.label = label;
    parsed.width = width;
    parsed.height = height;
    if (!解析点阵(pixelText, width, height, &parsed.bits)) {
        *error = "字库十六进制点阵长度或字符无效";
        return false;
    }
    parsed.foregroundCount = static_cast<int>(std::count(parsed.bits.begin(), parsed.bits.end(), 1));
    if (parsed.foregroundCount <= 0) {
        *error = "字库字形不能全为空白";
        return false;
    }

    parsed.foregroundOffsets.reserve(static_cast<size_t>(parsed.foregroundCount));
    for (size_t offset = 0; offset < parsed.bits.size(); ++offset) {
        if (parsed.bits[offset] != 0) {
            parsed.foregroundOffsets.push_back(static_cast<int>(offset));
        }
    }
    size_t anchorStep = std::max<size_t>(
            1,
            (parsed.foregroundOffsets.size() + kMaxGlyphAnchorCount - 1) / kMaxGlyphAnchorCount
    );
    for (size_t index = 0; index < parsed.foregroundOffsets.size(); index += anchorStep) {
        parsed.anchors.push_back(parsed.foregroundOffsets[index]);
    }
    if (parsed.anchors.back() != parsed.foregroundOffsets.back()) {
        parsed.anchors.push_back(parsed.foregroundOffsets.back());
    }
    *glyph = std::move(parsed);
    return true;
}

/** 从普通文件、ALPKG 资源或直接文本读取字库内容。 */
bool 读取字库文本(const char* dictionary, std::string* text) {
    if (text == nullptr) return 设置字库错误("字库文本输出对象为空");
    *text = dictionary == nullptr ? "" : dictionary;
    if (text->empty()) return 设置字库错误("字库内容不能为空");
    if (text->find('\n') != std::string::npos || text->find('\r') != std::string::npos) return true;

    std::ifstream file(*text, std::ios::binary);
    if (file) {
        std::ostringstream output;
        output << file.rdbuf();
        *text = output.str();
        return true;
    }

    std::vector<unsigned char> resource;
    std::string resourceError;
    if (readActiveAlpkgResource(*text, &resource, &resourceError)) {
        text->assign(reinterpret_cast<const char*>(resource.data()), resource.size());
        return true;
    }
    // 没有文件时按单条字库记录处理，便于脚本动态构建一个字符。
    return true;
}

/** 将字库文本解析为字形列表；空行和 # 注释行允许存在。 */
bool 解析字库文本(const std::string& text, std::vector<字形>* glyphs) {
    if (glyphs == nullptr) return 设置字库错误("字库字形输出对象为空");
    std::istringstream input(text);
    std::string line;
    int lineNumber = 0;
    std::vector<字形> parsed;
    while (std::getline(input, line)) {
        ++lineNumber;
        std::string trimmed = 去空白(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;
        字形 glyph;
        std::string error;
        if (!解析字库记录(trimmed, &glyph, &error)) {
            return 设置字库错误("字库第 " + std::to_string(lineNumber) + " 行无效：" + error);
        }
        parsed.push_back(std::move(glyph));
    }
    if (parsed.empty()) return 设置字库错误("字库中没有有效字形");
    *glyphs = std::move(parsed);
    return true;
}

/** 解析颜色规格，例如 FFFFFF-101010。前半段是目标色，后半段是 RGB 容差。 */
bool 解析颜色规则(const char* color, 颜色规则* rule) {
    if (rule == nullptr) return false;
    std::string value = 去空白(color == nullptr ? "" : color);
    if (value.empty()) return false;
    size_t separator = value.find('-');
    std::string target = separator == std::string::npos ? value : value.substr(0, separator);
    std::string delta = separator == std::string::npos ? "000000" : value.substr(separator + 1);
    if (target.rfind("0x", 0) == 0 || target.rfind("0X", 0) == 0) target.erase(0, 2);
    if (delta.rfind("0x", 0) == 0 || delta.rfind("0X", 0) == 0) delta.erase(0, 2);
    if (target.size() != 6 || delta.size() != 6) return false;
    int values[6] = {};
    for (int index = 0; index < 6; ++index) {
        int high = 十六进制值((index < 3 ? target : delta)[static_cast<size_t>((index % 3) * 2)]);
        int low = 十六进制值((index < 3 ? target : delta)[static_cast<size_t>((index % 3) * 2 + 1)]);
        if (high < 0 || low < 0) return false;
        values[index] = high * 16 + low;
    }
    rule->r = values[0];
    rule->g = values[1];
    rule->b = values[2];
    rule->dr = values[3];
    rule->dg = values[4];
    rule->db = values[5];
    return true;
}

inline bool 是前景(const unsigned char* rgba, const 颜色规则& rule) {
    return std::abs(static_cast<int>(rgba[0]) - rule.r) <= rule.dr
            && std::abs(static_cast<int>(rgba[1]) - rule.g) <= rule.dg
            && std::abs(static_cast<int>(rgba[2]) - rule.b) <= rule.db;
}

/** 把搜索矩形裁剪到截图尺寸内并按颜色规则二值化。 */
bool 二值化截图(
        int x1,
        int y1,
        int x2,
        int y2,
        const 颜色规则& rule,
        二值区域* output
) {
    if (output == nullptr) return 设置字库错误("二值化输出对象为空");
    ScreenFrame frame;
    if (!captureScreen(&frame)) return 设置字库错误(screenLastError());
    int left = std::max(0, std::min(x1, x2));
    int top = std::max(0, std::min(y1, y2));
    int right = std::min(frame.width - 1, std::max(x1, x2));
    int bottom = std::min(frame.height - 1, std::max(y1, y2));
    if (left > right || top > bottom) return 设置字库错误("识字区域无效");

    二值区域 region;
    region.left = left;
    region.top = top;
    region.width = right - left + 1;
    region.height = bottom - top + 1;
    region.bits.assign(static_cast<size_t>(region.width) * static_cast<size_t>(region.height), 0);
    region.foregroundOffsets.reserve(region.bits.size() / 8);
    for (int y = 0; y < region.height; ++y) {
        for (int x = 0; x < region.width; ++x) {
            const unsigned char* rgba = frame.pixels
                    + (static_cast<size_t>(top + y) * static_cast<size_t>(frame.width)
                            + static_cast<size_t>(left + x)) * kRgbaPixelBytes;
            size_t offset = static_cast<size_t>(y) * static_cast<size_t>(region.width)
                    + static_cast<size_t>(x);
            region.bits[offset] = 是前景(rgba, rule) ? 1 : 0;
            if (region.bits[offset] != 0) {
                region.foregroundOffsets.push_back(static_cast<int>(offset));
            }
        }
    }

    // 窗口前景数由积分图 O(1) 得到。找字会频繁核对候选字形的外接矩形，不能每次再扫描
    // width*height 个像素。
    const int stride = region.width + 1;
    region.integral.assign(static_cast<size_t>(stride) * static_cast<size_t>(region.height + 1), 0);
    for (int y = 0; y < region.height; ++y) {
        int rowSum = 0;
        for (int x = 0; x < region.width; ++x) {
            rowSum += region.bits[static_cast<size_t>(y) * static_cast<size_t>(region.width)
                    + static_cast<size_t>(x)];
            region.integral[static_cast<size_t>(y + 1) * static_cast<size_t>(stride)
                    + static_cast<size_t>(x + 1)]
                    = region.integral[static_cast<size_t>(y) * static_cast<size_t>(stride)
                    + static_cast<size_t>(x + 1)] + rowSum;
        }
    }
    *output = std::move(region);
    return true;
}

/** 返回当前线程正在使用的字库共享引用。 */
std::shared_ptr<字库> 当前字库() {
    std::lock_guard<std::mutex> lock(gDictionaryMutex);
    auto iterator = gDictionaries.find(gActiveDictionaryIndex);
    return iterator == gDictionaries.end() ? nullptr : iterator->second;
}

/** 构造 OCR 结果 JSON：items 保留每个字的坐标，text 按估算行距拼接。 */
std::string 识字项转JSON(const std::vector<识字项>& items) {
    std::vector<JsonValue> jsonItems;
    std::map<std::string, JsonValue> item;
    std::string text;
    int previousY = -1;
    int previousHeight = 0;
    for (const 识字项& value : items) {
        if (previousY >= 0 && std::abs(value.y - previousY) > std::max(4, previousHeight / 2)) text.push_back('\n');
        text += value.text;
        previousY = value.y;
        previousHeight = value.height;
        item.clear();
        item["text"] = JsonValue::makeString(value.text);
        item["x"] = JsonValue::makeNumber(value.x);
        item["y"] = JsonValue::makeNumber(value.y);
        item["w"] = JsonValue::makeNumber(value.width);
        item["h"] = JsonValue::makeNumber(value.height);
        item["score"] = JsonValue::makeNumber(value.score);
        jsonItems.push_back(JsonValue::makeObject(item));
    }
    std::map<std::string, JsonValue> result;
    result["text"] = JsonValue::makeString(text);
    result["items"] = JsonValue::makeArray(std::move(jsonItems));
    return jsonValueToString(JsonValue::makeObject(std::move(result)));
}

/** 返回二值区域中一个完整窗口的前景像素数；调用方保证窗口完全在区域内。 */
int 取窗口前景数(const 二值区域& region, int x, int y, int width, int height) {
    const int stride = region.width + 1;
    const size_t topLeft = static_cast<size_t>(y) * static_cast<size_t>(stride) + static_cast<size_t>(x);
    const size_t topRight = static_cast<size_t>(y) * static_cast<size_t>(stride) + static_cast<size_t>(x + width);
    const size_t bottomLeft = static_cast<size_t>(y + height) * static_cast<size_t>(stride) + static_cast<size_t>(x);
    const size_t bottomRight = static_cast<size_t>(y + height) * static_cast<size_t>(stride)
            + static_cast<size_t>(x + width);
    return region.integral[bottomRight] - region.integral[topRight]
            - region.integral[bottomLeft] + region.integral[topLeft];
}

/**
 * 计算一个字形在候选左上角的 Jaccard 点阵分数。
 *
 * 先用少量前景锚点排除绝大多数不命中位置，再完整核对字形前景；窗口前景总数由积分图取得，
 * 因此稀疏字形不会因为大面积背景相同而产生误命中。
 */
double 计算字形匹配分数(
        const 二值区域& region,
        const 字形& glyph,
        int x,
        int y,
        int windowForeground,
        double similarity
) {
    const int requiredAnchors = static_cast<int>(std::ceil(
            similarity * static_cast<double>(glyph.anchors.size())
    ));
    int anchorMatches = 0;
    for (size_t index = 0; index < glyph.anchors.size(); ++index) {
        int offset = glyph.anchors[index];
        int glyphX = offset % glyph.width;
        int glyphY = offset / glyph.width;
        if (region.bits[static_cast<size_t>(y + glyphY) * static_cast<size_t>(region.width)
                + static_cast<size_t>(x + glyphX)] != 0) {
            ++anchorMatches;
        }
        int remaining = static_cast<int>(glyph.anchors.size() - index - 1);
        if (anchorMatches + remaining < requiredAnchors) {
            return 0.0;
        }
    }

    const int requiredForeground = static_cast<int>(std::ceil(
            similarity * static_cast<double>(glyph.foregroundCount)
    ));
    int intersection = 0;
    for (size_t index = 0; index < glyph.foregroundOffsets.size(); ++index) {
        int offset = glyph.foregroundOffsets[index];
        int glyphX = offset % glyph.width;
        int glyphY = offset / glyph.width;
        if (region.bits[static_cast<size_t>(y + glyphY) * static_cast<size_t>(region.width)
                + static_cast<size_t>(x + glyphX)] != 0) {
            ++intersection;
        }
        int remaining = static_cast<int>(glyph.foregroundOffsets.size() - index - 1);
        if (intersection + remaining < requiredForeground) {
            return 0.0;
        }
    }

    int unionCount = glyph.foregroundCount + windowForeground - intersection;
    return unionCount <= 0 ? 0.0 : intersection / static_cast<double>(unionCount);
}

/** 在二值区域中扫描一个字形。候选只从实际前景点反推，不在整块背景逐像素盲扫。 */
void 搜索单个字形(
        const 二值区域& region,
        const 字形& glyph,
        double similarity,
        std::vector<识字项>* items
) {
    if (items == nullptr || glyph.anchors.empty() || glyph.width > region.width || glyph.height > region.height) {
        return;
    }

    int anchorOffset = glyph.anchors[glyph.anchors.size() / 2];
    int anchorX = anchorOffset % glyph.width;
    int anchorY = anchorOffset / glyph.width;
    int minimumForeground = static_cast<int>(std::ceil(similarity * glyph.foregroundCount));

    for (int screenOffset : region.foregroundOffsets) {
        int screenX = screenOffset % region.width;
        int screenY = screenOffset / region.width;
        int x = screenX - anchorX;
        int y = screenY - anchorY;
        if (x < 0 || y < 0 || x + glyph.width > region.width || y + glyph.height > region.height) {
            continue;
        }

        int windowForeground = 取窗口前景数(region, x, y, glyph.width, glyph.height);
        if (windowForeground < minimumForeground
                || static_cast<double>(glyph.foregroundCount) / std::max(1, windowForeground) < similarity) {
            continue;
        }
        double score = 计算字形匹配分数(region, glyph, x, y, windowForeground, similarity);
        if (score < similarity) {
            continue;
        }
        items->push_back({
                glyph.label,
                region.left + x,
                region.top + y,
                glyph.width,
                glyph.height,
                score
        });
        if (items->size() >= kMaxFontMatches) {
            return;
        }
    }
}

/** 两个候选框大面积重叠时表示同一字符的相邻像素候选，只保留分数更高的一个。 */
bool 是重复匹配(const 识字项& left, const 识字项& right) {
    int overlapWidth = std::min(left.x + left.width, right.x + right.width) - std::max(left.x, right.x);
    int overlapHeight = std::min(left.y + left.height, right.y + right.height) - std::max(left.y, right.y);
    if (overlapWidth <= 0 || overlapHeight <= 0) {
        return false;
    }
    int overlap = overlapWidth * overlapHeight;
    int smallerArea = std::min(left.width * left.height, right.width * right.height);
    return overlap * 2 >= smallerArea;
}

/** 按分数去重，再按自然阅读顺序排序。 */
void 整理识字项(std::vector<识字项>* items) {
    if (items == nullptr) {
        return;
    }
    std::sort(items->begin(), items->end(), [](const 识字项& left, const 识字项& right) {
        if (left.score != right.score) return left.score > right.score;
        if (left.y != right.y) return left.y < right.y;
        return left.x < right.x;
    });
    std::vector<识字项> unique;
    unique.reserve(items->size());
    for (const 识字项& item : *items) {
        bool duplicate = false;
        for (const 识字项& accepted : unique) {
            if (是重复匹配(item, accepted)) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            unique.push_back(item);
        }
    }
    std::sort(unique.begin(), unique.end(), [](const 识字项& left, const 识字项& right) {
        int tolerance = std::max(4, std::min(left.height, right.height) / 2);
        if (std::abs(left.y - right.y) <= tolerance) return left.x < right.x;
        return left.y < right.y;
    });
    *items = std::move(unique);
}

/**
 * 对二值区域进行直接点阵识字。
 *
 * 直接匹配不依赖笔画必须连通，因此能识别中文、i、: 等由多块前景构成的字形。字库很大时
 * 应优先使用找字接口，它只扫描目标文字涉及的字形，避免不必要地遍历整套字库。
 */
bool 识别区域(const 二值区域& region, double similarity, std::vector<识字项>* items) {
    if (items == nullptr) return 设置字库错误("识字结果输出对象为空");
    std::shared_ptr<字库> dictionary = 当前字库();
    if (dictionary == nullptr || dictionary->glyphs.empty()) return 设置字库错误("当前线程尚未选择有效字库");

    items->clear();
    for (const 字形& glyph : dictionary->glyphs) {
        搜索单个字形(region, glyph, similarity, items);
        if (items->size() >= kMaxFontMatches) {
            break;
        }
    }
    整理识字项(items);
    return true;
}

/** 把目标文本按当前字库中的 UTF-8 标签拆成字形序列，优先匹配更长标签。 */
bool 拆分目标字形(
        const 字库& dictionary,
        const std::string& text,
        std::vector<const 字形*>* glyphs
) {
    if (glyphs == nullptr) return 设置字库错误("目标字形输出对象为空");
    std::vector<const 字形*> labels;
    labels.reserve(dictionary.glyphs.size());
    for (const 字形& glyph : dictionary.glyphs) {
        labels.push_back(&glyph);
    }
    std::sort(labels.begin(), labels.end(), [](const 字形* left, const 字形* right) {
        return left->label.size() > right->label.size();
    });

    glyphs->clear();
    size_t offset = 0;
    while (offset < text.size()) {
        const 字形* matched = nullptr;
        for (const 字形* glyph : labels) {
            if (text.compare(offset, glyph->label.size(), glyph->label) == 0) {
                matched = glyph;
                break;
            }
        }
        if (matched == nullptr) {
            return 设置字库错误("当前字库没有目标文字：" + text.substr(offset));
        }
        glyphs->push_back(matched);
        offset += matched->label.size();
    }
    return true;
}

/** 在单行内串联目标字形候选，允许正常的字符间空白，不允许跨越无关文字。 */
std::vector<字库坐标> 查找目标文字(
        const 二值区域& region,
        const std::string& text,
        double similarity
) {
    std::vector<字库坐标> found;
    gFontLastError.clear();
    std::shared_ptr<字库> dictionary = 当前字库();
    if (dictionary == nullptr || dictionary->glyphs.empty()) {
        设置字库错误("当前线程尚未选择有效字库");
        return found;
    }

    std::vector<const 字形*> targetGlyphs;
    if (!拆分目标字形(*dictionary, text, &targetGlyphs)) {
        return found;
    }
    std::vector<std::vector<识字项>> candidates(targetGlyphs.size());
    for (size_t index = 0; index < targetGlyphs.size(); ++index) {
        搜索单个字形(region, *targetGlyphs[index], similarity, &candidates[index]);
        整理识字项(&candidates[index]);
        if (candidates[index].empty()) {
            return found;
        }
    }

    for (const 识字项& first : candidates.front()) {
        const 识字项* previous = &first;
        bool matched = true;
        for (size_t index = 1; index < candidates.size(); ++index) {
            const 识字项* next = nullptr;
            int maximumGap = std::max(4, std::max(previous->height, targetGlyphs[index]->height) / 2);
            for (const 识字项& candidate : candidates[index]) {
                int gap = candidate.x - (previous->x + previous->width);
                int lineTolerance = std::max(3, std::min(previous->height, candidate.height) / 3);
                if (gap >= -1 && gap <= maximumGap && std::abs(candidate.y - previous->y) <= lineTolerance) {
                    if (next == nullptr || candidate.x < next->x) {
                        next = &candidate;
                    }
                }
            }
            if (next == nullptr) {
                matched = false;
                break;
            }
            previous = next;
        }
        if (matched) {
            found.push_back({first.x, first.y});
        }
    }

    std::sort(found.begin(), found.end(), [](const 字库坐标& left, const 字库坐标& right) {
        if (left.y != right.y) return left.y < right.y;
        return left.x < right.x;
    });
    found.erase(std::unique(found.begin(), found.end(), [](const 字库坐标& left, const 字库坐标& right) {
        return left.x == right.x && left.y == right.y;
    }), found.end());
    return found;
}

/** 将一位点阵编码为稳定十六进制文本，行优先且最高位在前。 */
std::string 点阵转十六进制(const std::vector<unsigned char>& bits) {
    static const char* digits = "0123456789ABCDEF";
    std::string output;
    output.reserve((bits.size() + 3) / 4);
    for (size_t start = 0; start < bits.size(); start += 4) {
        int value = 0;
        for (size_t bit = 0; bit < 4; ++bit) {
            value <<= 1;
            if (start + bit < bits.size() && bits[start + bit] != 0) value |= 1;
        }
        output.push_back(digits[value]);
    }
    return output;
}

} // namespace

bool 设置字库(int index, const char* dictionary) {
    if (!校验字库索引(index)) return false;
    std::string text;
    if (!读取字库文本(dictionary, &text)) return false;
    std::vector<字形> glyphs;
    if (!解析字库文本(text, &glyphs)) return false;
    auto target = std::make_shared<字库>();
    target->glyphs = std::move(glyphs);
    {
        std::lock_guard<std::mutex> lock(gDictionaryMutex);
        gDictionaries[index] = target;
    }
    gFontLastError.clear();
    return true;
}

bool 追加字库(int index, const char* dictionary) {
    if (!校验字库索引(index)) return false;
    std::string text;
    if (!读取字库文本(dictionary, &text)) return false;
    std::vector<字形> appended;
    if (!解析字库文本(text, &appended)) return false;
    std::lock_guard<std::mutex> lock(gDictionaryMutex);
    // 识字线程会在锁外持有 shared_ptr 扫描字形，因此追加时必须复制后替换，不能直接修改
    // 旧 vector，否则并发识字会遇到扩容后的悬空地址。
    auto target = std::make_shared<字库>();
    auto iterator = gDictionaries.find(index);
    if (iterator != gDictionaries.end() && iterator->second != nullptr) {
        target->glyphs = iterator->second->glyphs;
    }
    target->glyphs.insert(target->glyphs.end(), appended.begin(), appended.end());
    gDictionaries[index] = std::move(target);
    gFontLastError.clear();
    return true;
}

bool 使用字库(int index) {
    if (!校验字库索引(index)) return false;
    {
        std::lock_guard<std::mutex> lock(gDictionaryMutex);
        if (gDictionaries.find(index) == gDictionaries.end()) return 设置字库错误("指定字库尚未加载");
    }
    gActiveDictionaryIndex = index;
    gFontLastError.clear();
    return true;
}

bool 获取字形点阵(
        int x1,
        int y1,
        int x2,
        int y2,
        const char* color,
        std::string* fontPixel
) {
    if (fontPixel == nullptr) return 设置字库错误("字形点阵输出对象为空");
    颜色规则 rule;
    if (!解析颜色规则(color, &rule)) return 设置字库错误("字形颜色格式无效，应为 FFFFFF-101010");
    二值区域 region;
    if (!二值化截图(x1, y1, x2, y2, rule, &region)) return false;
    int left = region.width;
    int top = region.height;
    int right = -1;
    int bottom = -1;
    for (int y = 0; y < region.height; ++y) {
        for (int x = 0; x < region.width; ++x) {
            if (region.bits[static_cast<size_t>(y) * static_cast<size_t>(region.width) + static_cast<size_t>(x)] == 0) continue;
            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x);
            bottom = std::max(bottom, y);
        }
    }
    if (right < left || bottom < top) return 设置字库错误("指定区域没有符合颜色的字形点阵");
    int width = right - left + 1;
    int height = bottom - top + 1;
    std::vector<unsigned char> bits(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
    for (int y = 0; y < height; ++y) {
        std::copy_n(
                region.bits.data() + static_cast<size_t>(top + y) * static_cast<size_t>(region.width) + static_cast<size_t>(left),
                width,
                bits.data() + static_cast<size_t>(y) * static_cast<size_t>(width)
        );
    }
    *fontPixel = std::to_string(width) + "$" + std::to_string(height) + "$" + 点阵转十六进制(bits);
    gFontLastError.clear();
    return true;
}

bool 点阵识字(
        int x1,
        int y1,
        int x2,
        int y2,
        const char* color,
        double similarity,
        std::string* resultJson
) {
    if (resultJson == nullptr) return 设置字库错误("识字 JSON 输出对象为空");
    if (!std::isfinite(similarity) || similarity <= 0.0 || similarity > 1.0) {
        return 设置字库错误("字库相似度必须大于 0 且不超过 1");
    }
    颜色规则 rule;
    if (!解析颜色规则(color, &rule)) return 设置字库错误("字形颜色格式无效，应为 FFFFFF-101010");
    二值区域 region;
    if (!二值化截图(x1, y1, x2, y2, rule, &region)) return false;
    std::vector<识字项> items;
    if (!识别区域(region, similarity, &items)) return false;
    *resultJson = 识字项转JSON(items);
    gFontLastError.clear();
    return true;
}

bool 点阵找字(
        int x1,
        int y1,
        int x2,
        int y2,
        const char* text,
        const char* color,
        double similarity,
        字库坐标* point
) {
    if (point == nullptr) return 设置字库错误("找字坐标输出对象为空");
    point->x = -1;
    point->y = -1;
    std::string target = text == nullptr ? "" : text;
    if (target.empty()) return 设置字库错误("要查找的文字不能为空");
    if (!std::isfinite(similarity) || similarity <= 0.0 || similarity > 1.0) {
        return 设置字库错误("字库相似度必须大于 0 且不超过 1");
    }
    颜色规则 rule;
    if (!解析颜色规则(color, &rule)) return 设置字库错误("字形颜色格式无效，应为 FFFFFF-101010");
    二值区域 region;
    if (!二值化截图(x1, y1, x2, y2, rule, &region)) return false;
    std::vector<字库坐标> found = 查找目标文字(region, target, similarity);
    if (!gFontLastError.empty()) return false;
    if (found.empty()) {
        gFontLastError.clear();
        return false;
    }
    *point = found.front();
    gFontLastError.clear();
    return true;
}

bool 点阵找字全部(
        int x1,
        int y1,
        int x2,
        int y2,
        const char* text,
        const char* color,
        double similarity,
        std::string* resultJson
) {
    if (resultJson == nullptr) return 设置字库错误("找字结果 JSON 输出对象为空");
    std::string target = text == nullptr ? "" : text;
    if (target.empty()) return 设置字库错误("要查找的文字不能为空");
    if (!std::isfinite(similarity) || similarity <= 0.0 || similarity > 1.0) {
        return 设置字库错误("字库相似度必须大于 0 且不超过 1");
    }
    颜色规则 rule;
    if (!解析颜色规则(color, &rule)) return 设置字库错误("字形颜色格式无效，应为 FFFFFF-101010");
    二值区域 region;
    if (!二值化截图(x1, y1, x2, y2, rule, &region)) return false;
    std::vector<字库坐标> found = 查找目标文字(region, target, similarity);
    if (!gFontLastError.empty()) return false;
    std::vector<JsonValue> values;
    for (const 字库坐标& point : found) {
        std::map<std::string, JsonValue> item;
        item["x"] = JsonValue::makeNumber(point.x);
        item["y"] = JsonValue::makeNumber(point.y);
        values.push_back(JsonValue::makeObject(std::move(item)));
    }
    *resultJson = jsonValueToString(JsonValue::makeArray(std::move(values)));
    gFontLastError.clear();
    return true;
}

std::string 取字库错误() {
    return gFontLastError;
}

} // namespace xiaoyv::api
