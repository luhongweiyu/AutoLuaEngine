/**
 * 文件用途：实现自定义点阵字库的加载、11 位候选索引、64 位完整点阵识别和找字。
 */
#include "font_api.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "package_api.h"
#include "screen_api.h"
#include "../../engine/json_value.h"
#include "native/image_core/image_core.h"

namespace xiaoyv::api {
namespace {

constexpr int kMaxDictionaryIndex = 255;
constexpr int kMaxGlyphWidth = 256;
constexpr int kMaxGlyphHeight = 256;
constexpr int kSimplifiedLegacyHeight = 11;
constexpr int kSignatureBits = 11;
constexpr int kSignatureValueCount = 1 << kSignatureBits;
constexpr int kPackedWordBits = 64;
constexpr int kRgbaPixelBytes = 4;
constexpr size_t kSignatureCandidateLimit = 16;
constexpr size_t kMaxFontMatches = 8192;

/** 字形用于快速定位候选位置的一段纵向点阵。 */
struct 十一位特征 {
    uint16_t value = 0;
    uint16_t mask = 0;
    int x = 0;
    int y = 0;
    int length = 0;
    int quality = 0;
};

/**
 * 一条完整字形。
 *
 * packedRows 按原始二维行保存全部点阵，每行补齐到 64 位。十一位特征只负责定位候选，
 * 最终结果始终由 packedRows 完整比较决定，不能把 11 位特征当作字形高度。
 */
struct 字形 {
    std::string label;
    int width = 0;
    int height = 0;
    int foregroundCount = 0;
    int wordsPerRow = 0;
    std::vector<uint64_t> packedRows;
    十一位特征 signature;
};

/** 一个字库索引下的不可变字形快照。 */
struct 字库 {
    std::vector<字形> glyphs;
    std::unordered_map<std::string, std::vector<size_t>> glyphIndicesByLabel;
    std::vector<std::string> labelsLongestFirst;
};

/**
 * 二值化后的截图区域。
 *
 * packedRows 用于 64 位完整比较；signatureBuckets 按纵向 11 位值保存屏幕坐标，字形无需
 * 再逐个遍历整片前景点。每个坐标编码为 y * width + x。
 */
struct 二值区域 {
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
    int wordsPerRow = 0;
    std::vector<uint64_t> packedRows;
    std::array<std::vector<int>, kSignatureValueCount> signatureBuckets;
};

/** 已匹配的字形、坐标和完整点阵得分。 */
struct 识字项 {
    std::string text;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int foregroundCount = 0;
    double score = 0.0;
};

/** RGB 目标色和每个通道允许的差值。 */
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

/** 按 $ 切分字库行；字形标签本身不能包含 $。 */
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

/** 删除点阵中的空格、制表符和逗号，并校验剩余内容都是十六进制。 */
bool 规范十六进制点阵(const std::string& source, std::string* output) {
    if (output == nullptr) return false;
    output->clear();
    output->reserve(source.size());
    for (char character : source) {
        if (character == ' ' || character == '\t' || character == ',') continue;
        if (十六进制值(character) < 0) return false;
        output->push_back(character);
    }
    return !output->empty();
}

inline int 统计位数(uint64_t value) {
    return __builtin_popcountll(value);
}

inline bool 读取点阵位(
        const std::vector<uint64_t>& rows,
        int wordsPerRow,
        int x,
        int y
) {
    size_t index = static_cast<size_t>(y) * static_cast<size_t>(wordsPerRow)
            + static_cast<size_t>(x / kPackedWordBits);
    return ((rows[index] >> (x % kPackedWordBits)) & 1ULL) != 0ULL;
}

inline void 写入点阵位(std::vector<uint64_t>* rows, int wordsPerRow, int x, int y) {
    size_t index = static_cast<size_t>(y) * static_cast<size_t>(wordsPerRow)
            + static_cast<size_t>(x / kPackedWordBits);
    (*rows)[index] |= 1ULL << (x % kPackedWordBits);
}

uint64_t 末字有效掩码(int width) {
    int remaining = width % kPackedWordBits;
    return remaining == 0 ? std::numeric_limits<uint64_t>::max() : ((1ULL << remaining) - 1ULL);
}

/** 把行优先十六进制点阵转换为适合位运算的 64 位行数据。 */
bool 解析完整点阵(const std::string& source, int width, int height, 字形* glyph) {
    if (glyph == nullptr || width <= 0 || height <= 0) return false;
    std::string hex;
    if (!规范十六进制点阵(source, &hex)) return false;
    long long neededBits = static_cast<long long>(width) * static_cast<long long>(height);
    if (static_cast<long long>(hex.size()) * 4LL < neededBits) return false;

    glyph->width = width;
    glyph->height = height;
    glyph->wordsPerRow = (width + kPackedWordBits - 1) / kPackedWordBits;
    glyph->packedRows.assign(
            static_cast<size_t>(glyph->wordsPerRow) * static_cast<size_t>(height),
            0ULL
    );
    glyph->foregroundCount = 0;
    for (long long offset = 0; offset < neededBits; ++offset) {
        int value = 十六进制值(hex[static_cast<size_t>(offset / 4)]);
        if (((value >> (3 - static_cast<int>(offset % 4))) & 1) == 0) continue;
        int y = static_cast<int>(offset / width);
        int x = static_cast<int>(offset % width);
        写入点阵位(&glyph->packedRows, glyph->wordsPerRow, x, y);
        ++glyph->foregroundCount;
    }
    return glyph->foregroundCount > 0;
}

/**
 * 解析一条字库记录。
 *
 * 支持三类格式：
 * - 小鱼格式：文字$宽$高$十六进制点阵；
 * - 懒人格式：文字$十六进制点阵$元数据...$真实高度；
 * - 大漠格式：十六进制点阵$文字$偏移元数据$真实高度。
 *
 * 旧格式点阵会按真实高度恢复宽度。十六进制及 11 位分组产生的尾部补零不属于字形。
 */
bool 解析字库记录(const std::string& rawLine, 字形* glyph, std::string* error) {
    if (glyph == nullptr || error == nullptr) return false;
    std::string line = 去空白(rawLine);
    if (line.empty() || line[0] == '#') return false;
    std::vector<std::string> parts = 分割字库行(line);
    if (parts.size() < 2) {
        *error = "字库行至少需要文字和点阵";
        return false;
    }

    std::string label;
    std::string pixelText;
    int width = 0;
    int height = 0;

    // 小鱼格式必须同时具有明确宽度、高度和第四段点阵，优先级最高，不会与旧格式混淆。
    if (parts.size() == 4
            && 解析正整数(parts[1], &width)
            && 解析正整数(parts[2], &height)) {
        label = parts[0];
        pixelText = parts[3];
    } else {
        std::string firstHex;
        std::string secondHex;
        bool firstIsHex = 规范十六进制点阵(parts[0], &firstHex);
        bool secondIsHex = 规范十六进制点阵(parts[1], &secondHex);

        height = kSimplifiedLegacyHeight;
        if (parts.size() >= 3 && !解析正整数(parts.back(), &height)) {
            height = kSimplifiedLegacyHeight;
        }

        // 标签恰好是 A、B、1 等十六进制字符时也不能误判。旧格式中点阵通常远长于标签，
        // 因此只有第一段是明显的长点阵或第二段不是点阵时才按大漠顺序解析。
        bool dmOrder = parts.size() >= 4
                && firstIsHex
                && (!secondIsHex || firstHex.size() > secondHex.size());
        if (dmOrder) {
            pixelText = parts[0];
            label = parts[1];
        } else {
            label = parts[0];
            pixelText = parts[1];
        }

        std::string compact;
        if (!规范十六进制点阵(pixelText, &compact)) {
            *error = "旧字库点阵不是有效十六进制";
            return false;
        }
        long long storedBits = static_cast<long long>(compact.size()) * 4LL;
        if (height <= 0 || storedBits < height) {
            *error = "旧字库点阵长度不足以恢复字形宽度";
            return false;
        }
        width = static_cast<int>(storedBits / height);
    }

    label = 去空白(label);
    if (label.empty()) {
        *error = "字库文字不能为空";
        return false;
    }
    if (width <= 0 || height <= 0 || width > kMaxGlyphWidth || height > kMaxGlyphHeight) {
        *error = "字库字形宽高必须在 1 到 256 之间";
        return false;
    }

    字形 parsed;
    parsed.label = label;
    if (!解析完整点阵(pixelText, width, height, &parsed)) {
        *error = "字库点阵长度无效或字形全为空白";
        return false;
    }
    *glyph = std::move(parsed);
    return true;
}

/** 从普通文件、当前 ALPKG 资源或直接文本读取字库内容。 */
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
    }
    // 路径和包资源都不存在时按单条字库记录处理，便于脚本动态创建字形。
    return true;
}

/** 解析多行字库；空行和 # 开头的注释行不会生成字形。 */
bool 解析字库文本(const std::string& text, std::vector<字形>* glyphs) {
    if (glyphs == nullptr) return 设置字库错误("字库字形输出对象为空");
    std::istringstream input(text);
    std::string line;
    int lineNumber = 0;
    std::vector<字形> parsed;
    while (std::getline(input, line)) {
        ++lineNumber;
        // Windows 编辑器常为 UTF-8 文本写入 BOM。BOM 只可能出现在文件第一行，必须在
        // 解析标签前移除，否则首个字形会得到不可见的三个前缀字节。
        if (lineNumber == 1 && line.size() >= 3
                && static_cast<unsigned char>(line[0]) == 0xEF
                && static_cast<unsigned char>(line[1]) == 0xBB
                && static_cast<unsigned char>(line[2]) == 0xBF) {
            line.erase(0, 3);
        }
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

/** 读取字形中一段纵向点阵，最低位对应最上方像素。 */
uint16_t 读取纵向特征(const 字形& glyph, int x, int y, int length) {
    uint16_t value = 0;
    for (int bit = 0; bit < length; ++bit) {
        if (读取点阵位(glyph.packedRows, glyph.wordsPerRow, x, y + bit)) {
            value |= static_cast<uint16_t>(1U << bit);
        }
    }
    return value;
}

/**
 * 为一个字形保留少量高信息特征候选。
 *
 * 优先选择同时包含前景和背景、上下变化较多且靠近字形中心的 11 位列。这样的特征比全 0、
 * 全 1 或固定首列更少与其他字形碰撞。高度不足 11 时使用完整字高并通过 mask 忽略下方位。
 */
std::vector<十一位特征> 生成特征候选(const 字形& glyph) {
    int length = std::min(kSignatureBits, glyph.height);
    uint16_t mask = static_cast<uint16_t>((1U << length) - 1U);
    std::array<十一位特征, kSignatureValueCount> bestByValue{};
    std::array<bool, kSignatureValueCount> hasValue{};
    int maximumY = std::max(0, glyph.height - length);

    for (int x = 0; x < glyph.width; ++x) {
        for (int y = 0; y <= maximumY; ++y) {
            uint16_t value = static_cast<uint16_t>(读取纵向特征(glyph, x, y, length) & mask);
            int ones = 统计位数(value);
            if (ones == 0) continue;
            int transitions = 0;
            for (int bit = 1; bit < length; ++bit) {
                if (((value >> bit) & 1U) != ((value >> (bit - 1)) & 1U)) ++transitions;
            }
            int balance = std::min(ones, length - ones);
            int centerDistance = std::abs(x * 2 - (glyph.width - 1))
                    + std::abs((y * 2 + length - 1) - (glyph.height - 1));
            int quality = balance * 100 + transitions * 20 + std::min(ones, 5) * 5 - centerDistance;
            十一位特征 candidate{value, mask, x, y, length, quality};
            if (!hasValue[value] || candidate.quality > bestByValue[value].quality) {
                bestByValue[value] = candidate;
                hasValue[value] = true;
            }
        }
    }

    std::vector<十一位特征> candidates;
    for (int value = 0; value < kSignatureValueCount; ++value) {
        if (hasValue[static_cast<size_t>(value)]) {
            candidates.push_back(bestByValue[static_cast<size_t>(value)]);
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const 十一位特征& left, const 十一位特征& right) {
        if (left.quality != right.quality) return left.quality > right.quality;
        return left.value < right.value;
    });
    if (candidates.size() > kSignatureCandidateLimit) {
        candidates.resize(kSignatureCandidateLimit);
    }
    return candidates;
}

/**
 * 为整套字库选择低碰撞特征。
 *
 * 先收集每个字形的高质量候选，再统计候选值在整套字库中的重复次数。最终优先使用更少重复
 * 的特征，重复次数相同时才比较字形内部质量。
 */
bool 构建字库特征(字库* dictionary) {
    if (dictionary == nullptr || dictionary->glyphs.empty()) {
        return 设置字库错误("字库中没有可构建索引的字形");
    }
    std::vector<std::vector<十一位特征>> allCandidates;
    allCandidates.reserve(dictionary->glyphs.size());
    std::array<int, kSignatureValueCount> frequency{};
    for (const 字形& glyph : dictionary->glyphs) {
        std::vector<十一位特征> candidates = 生成特征候选(glyph);
        if (candidates.empty()) return 设置字库错误("字库字形无法生成 11 位特征：" + glyph.label);
        for (const 十一位特征& candidate : candidates) {
            ++frequency[candidate.value];
        }
        allCandidates.push_back(std::move(candidates));
    }

    for (size_t index = 0; index < dictionary->glyphs.size(); ++index) {
        const std::vector<十一位特征>& candidates = allCandidates[index];
        const 十一位特征* selected = &candidates.front();
        for (const 十一位特征& candidate : candidates) {
            if (frequency[candidate.value] < frequency[selected->value]
                    || (frequency[candidate.value] == frequency[selected->value]
                            && candidate.quality > selected->quality)) {
                selected = &candidate;
            }
        }
        dictionary->glyphs[index].signature = *selected;
    }

    // 快速找字不能在每次调用时用线性去重重建标签表。字库加载阶段一次性建立标签到字形
    // 索引的映射，并预先按 UTF-8 字节长度降序排列标签，目标拆分只需查表。
    dictionary->glyphIndicesByLabel.clear();
    dictionary->glyphIndicesByLabel.reserve(dictionary->glyphs.size());
    for (size_t index = 0; index < dictionary->glyphs.size(); ++index) {
        dictionary->glyphIndicesByLabel[dictionary->glyphs[index].label].push_back(index);
    }
    dictionary->labelsLongestFirst.clear();
    dictionary->labelsLongestFirst.reserve(dictionary->glyphIndicesByLabel.size());
    for (const auto& entry : dictionary->glyphIndicesByLabel) {
        dictionary->labelsLongestFirst.push_back(entry.first);
    }
    std::sort(
            dictionary->labelsLongestFirst.begin(),
            dictionary->labelsLongestFirst.end(),
            [](const std::string& left, const std::string& right) {
                if (left.size() != right.size()) return left.size() > right.size();
                return left < right;
            }
    );
    return true;
}

/** 解析颜色规格，例如 FFFFFF-101010；后半段是每个 RGB 通道的允许差值。 */
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
        const std::string& source = index < 3 ? target : delta;
        int pair = index % 3;
        int high = 十六进制值(source[static_cast<size_t>(pair * 2)]);
        int low = 十六进制值(source[static_cast<size_t>(pair * 2 + 1)]);
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

/** 根据完整二值区域建立纵向滚动 11 位索引，每个屏幕坐标只进入一个桶。 */
void 构建屏幕特征索引(二值区域* region) {
    if (region == nullptr || region->width <= 0 || region->height <= 0) return;
    for (std::vector<int>& bucket : region->signatureBuckets) bucket.clear();
    size_t total = static_cast<size_t>(region->width) * static_cast<size_t>(region->height);
    size_t average = total / kSignatureValueCount;
    for (std::vector<int>& bucket : region->signatureBuckets) bucket.reserve(average + 1);

    for (int x = 0; x < region->width; ++x) {
        uint16_t value = 0;
        int initialLength = std::min(kSignatureBits, region->height);
        for (int bit = 0; bit < initialLength; ++bit) {
            if (读取点阵位(region->packedRows, region->wordsPerRow, x, bit)) {
                value |= static_cast<uint16_t>(1U << bit);
            }
        }
        for (int y = 0; y < region->height; ++y) {
            region->signatureBuckets[value].push_back(y * region->width + x);
            value = static_cast<uint16_t>(value >> 1U);
            int nextY = y + kSignatureBits;
            if (nextY < region->height
                    && 读取点阵位(region->packedRows, region->wordsPerRow, x, nextY)) {
                value |= static_cast<uint16_t>(1U << (kSignatureBits - 1));
            }
        }
    }
}

/** 把截图指定区域转换为紧凑二值行；只有识字和找字才建立 11 位坐标桶。 */
bool 二值化截图(
        int x1,
        int y1,
        int x2,
        int y2,
        const 颜色规则& rule,
        bool buildSignatureIndex,
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
    region.wordsPerRow = (region.width + kPackedWordBits - 1) / kPackedWordBits;
    region.packedRows.assign(
            static_cast<size_t>(region.wordsPerRow) * static_cast<size_t>(region.height),
            0ULL
    );
    for (int y = 0; y < region.height; ++y) {
        for (int x = 0; x < region.width; ++x) {
            const unsigned char* rgba = frame.pixels
                    + (static_cast<size_t>(top + y) * static_cast<size_t>(frame.width)
                            + static_cast<size_t>(left + x)) * kRgbaPixelBytes;
            if (是前景(rgba, rule)) 写入点阵位(&region.packedRows, region.wordsPerRow, x, y);
        }
    }
    if (buildSignatureIndex) 构建屏幕特征索引(&region);
    *output = std::move(region);
    return true;
}

/** 返回当前线程选择的不可变字库快照。 */
std::shared_ptr<字库> 当前字库() {
    std::lock_guard<std::mutex> lock(gDictionaryMutex);
    auto iterator = gDictionaries.find(gActiveDictionaryIndex);
    return iterator == gDictionaries.end() ? nullptr : iterator->second;
}

/** 从二值区域某行提取从 x 开始的 64 位，跨字边界时合并相邻存储字。 */
uint64_t 读取区域行段(const 二值区域& region, int y, int x) {
    int wordIndex = x / kPackedWordBits;
    int shift = x % kPackedWordBits;
    size_t rowStart = static_cast<size_t>(y) * static_cast<size_t>(region.wordsPerRow);
    uint64_t value = region.packedRows[rowStart + static_cast<size_t>(wordIndex)] >> shift;
    if (shift != 0 && wordIndex + 1 < region.wordsPerRow) {
        value |= region.packedRows[rowStart + static_cast<size_t>(wordIndex + 1)]
                << (kPackedWordBits - shift);
    }
    return value;
}

/**
 * 使用完整点阵计算候选得分。
 *
 * 缺失点和多余点分别受 sim 约束，避免只检查字库前景而把相邻文字当作同一字形。所有计数
 * 均由 64 位 popcount 完成；返回负数表示不满足相似度。
 */
double 计算完整点阵得分(
        const 二值区域& region,
        const 字形& glyph,
        int x,
        int y,
        double similarity
) {
    int allowedErrors = static_cast<int>(std::floor(
            (1.0 - similarity) * static_cast<double>(glyph.foregroundCount) + 1e-9
    ));
    int missing = 0;
    int extra = 0;
    uint64_t lastMask = 末字有效掩码(glyph.width);
    for (int row = 0; row < glyph.height; ++row) {
        size_t glyphRow = static_cast<size_t>(row) * static_cast<size_t>(glyph.wordsPerRow);
        for (int word = 0; word < glyph.wordsPerRow; ++word) {
            uint64_t mask = word + 1 == glyph.wordsPerRow
                    ? lastMask
                    : std::numeric_limits<uint64_t>::max();
            uint64_t expected = glyph.packedRows[glyphRow + static_cast<size_t>(word)] & mask;
            uint64_t actual = 读取区域行段(region, y + row, x + word * kPackedWordBits) & mask;
            missing += 统计位数(expected & ~actual);
            extra += 统计位数(actual & ~expected);
            if (missing > allowedErrors || extra > allowedErrors) return -1.0;
        }
    }
    double errorRatio = std::max(missing, extra) / static_cast<double>(glyph.foregroundCount);
    return std::max(0.0, 1.0 - errorRatio);
}

/** 返回特征阶段允许的位差；完整点阵阶段仍以真实前景点总数进行最终判断。 */
int 特征允许位差(const 十一位特征& signature, double similarity) {
    if (similarity >= 1.0) return 0;
    return std::min(
            signature.length,
            std::max(1, static_cast<int>(std::ceil((1.0 - similarity) * signature.length - 1e-9)))
    );
}

/**
 * 返回 11 位完整特征在指定最大位差下需要读取的异或掩码。
 *
 * 表只初始化一次。精确匹配只含掩码 0；允许 1 位误差时只含 12 个掩码，避免每个字形都
 * 线性检查全部 2048 个桶。
 */
const std::array<std::vector<uint16_t>, kSignatureBits + 1>& 取得特征差分表() {
    static const std::array<std::vector<uint16_t>, kSignatureBits + 1> table = [] {
        std::array<std::vector<uint16_t>, kSignatureBits + 1> result;
        for (int value = 0; value < kSignatureValueCount; ++value) {
            int bitCount = 统计位数(static_cast<uint16_t>(value));
            for (int maximumErrors = bitCount; maximumErrors <= kSignatureBits; ++maximumErrors) {
                result[static_cast<size_t>(maximumErrors)].push_back(static_cast<uint16_t>(value));
            }
        }
        return result;
    }();
    return table;
}

/**
 * 借助 11 位屏幕桶搜索一个字形。
 *
 * 精确模式只读取一个桶；模糊模式最多检查 2048 个很小的桶编号，实际完整比较只发生在特征
 * 位差满足要求的位置。候选左上角由特征在字形中的偏移反推出。
 */
void 搜索单个字形(
        const 二值区域& region,
        const 字形& glyph,
        double similarity,
        std::vector<识字项>* items
) {
    if (items == nullptr || glyph.width > region.width || glyph.height > region.height) return;
    int signatureErrors = 特征允许位差(glyph.signature, similarity);
    auto searchBucket = [&](int value) {
        const std::vector<int>& positions = region.signatureBuckets[static_cast<size_t>(value)];
        for (int position : positions) {
            int signatureX = position % region.width;
            int signatureY = position / region.width;
            int x = signatureX - glyph.signature.x;
            int y = signatureY - glyph.signature.y;
            if (x < 0 || y < 0 || x + glyph.width > region.width || y + glyph.height > region.height) {
                continue;
            }
            double score = 计算完整点阵得分(region, glyph, x, y, similarity);
            if (score < similarity) continue;
            items->push_back({
                    glyph.label,
                    region.left + x,
                    region.top + y,
                    glyph.width,
                    glyph.height,
                    glyph.foregroundCount,
                    score
            });
            if (items->size() >= kMaxFontMatches) return false;
        }
        return true;
    };

    if (glyph.signature.length == kSignatureBits) {
        // 常规手机字形高度不小于 11，直接枚举真实允许的差分桶。精确模式只访问一个桶。
        const std::vector<uint16_t>& differences =
                取得特征差分表()[static_cast<size_t>(signatureErrors)];
        for (uint16_t difference : differences) {
            int value = static_cast<int>(glyph.signature.value ^ difference);
            if (!searchBucket(value)) return;
        }
        return;
    }

    // 高度不足 11 的字形只约束 mask 内的低位；屏幕桶中未被 mask 覆盖的高位不能参与位差。
    for (int value = 0; value < kSignatureValueCount; ++value) {
        uint16_t difference = static_cast<uint16_t>(
                (static_cast<uint16_t>(value) ^ glyph.signature.value) & glyph.signature.mask
        );
        if (统计位数(difference) <= signatureErrors && !searchBucket(value)) return;
    }
}

/** 大面积重叠表示同一位置的多个字形候选，只保留证据更强的一项。 */
bool 是重复匹配(const 识字项& left, const 识字项& right) {
    int overlapWidth = std::min(left.x + left.width, right.x + right.width) - std::max(left.x, right.x);
    int overlapHeight = std::min(left.y + left.height, right.y + right.height) - std::max(left.y, right.y);
    if (overlapWidth <= 0 || overlapHeight <= 0) return false;
    int overlap = overlapWidth * overlapHeight;
    int smallerArea = std::min(left.width * left.height, right.width * right.height);
    return overlap * 2 >= smallerArea;
}

/** 按得分去重，再按从上到下、从左到右的自然阅读顺序排列。 */
void 整理识字项(std::vector<识字项>* items) {
    if (items == nullptr) return;
    std::sort(items->begin(), items->end(), [](const 识字项& left, const 识字项& right) {
        if (left.score != right.score) return left.score > right.score;
        if (left.foregroundCount != right.foregroundCount) return left.foregroundCount > right.foregroundCount;
        if (left.text.size() != right.text.size()) return left.text.size() > right.text.size();
        if (left.y != right.y) return left.y < right.y;
        return left.x < right.x;
    });

    constexpr int kDeduplicateCellSize = 32;
    std::vector<识字项> unique;
    unique.reserve(items->size());
    std::unordered_map<long long, std::vector<size_t>> spatialIndex;
    std::vector<int> checkedGeneration;
    int generation = 0;
    for (const 识字项& item : *items) {
        ++generation;
        bool duplicate = false;
        int leftCell = item.x / kDeduplicateCellSize;
        int topCell = item.y / kDeduplicateCellSize;
        int rightCell = (item.x + item.width - 1) / kDeduplicateCellSize;
        int bottomCell = (item.y + item.height - 1) / kDeduplicateCellSize;
        for (int cellY = topCell; cellY <= bottomCell && !duplicate; ++cellY) {
            for (int cellX = leftCell; cellX <= rightCell && !duplicate; ++cellX) {
                long long key = (static_cast<long long>(cellY) << 32)
                        | static_cast<unsigned int>(cellX);
                auto iterator = spatialIndex.find(key);
                if (iterator == spatialIndex.end()) continue;
                for (size_t acceptedIndex : iterator->second) {
                    if (checkedGeneration[acceptedIndex] == generation) continue;
                    checkedGeneration[acceptedIndex] = generation;
                    if (是重复匹配(item, unique[acceptedIndex])) {
                        duplicate = true;
                        break;
                    }
                }
            }
        }
        if (duplicate) continue;

        size_t acceptedIndex = unique.size();
        unique.push_back(item);
        checkedGeneration.push_back(0);
        for (int cellY = topCell; cellY <= bottomCell; ++cellY) {
            for (int cellX = leftCell; cellX <= rightCell; ++cellX) {
                long long key = (static_cast<long long>(cellY) << 32)
                        | static_cast<unsigned int>(cellX);
                spatialIndex[key].push_back(acceptedIndex);
            }
        }
    }
    std::sort(unique.begin(), unique.end(), [](const 识字项& left, const 识字项& right) {
        int tolerance = std::max(4, std::min(left.height, right.height) / 2);
        if (std::abs(left.y - right.y) <= tolerance) return left.x < right.x;
        return left.y < right.y;
    });
    *items = std::move(unique);
}

/** 识别当前字库中的全部字形。 */
bool 识别全部字形(
        const 二值区域& region,
        const 字库& dictionary,
        double similarity,
        std::vector<识字项>* items
) {
    if (items == nullptr) return 设置字库错误("识字结果输出对象为空");
    items->clear();
    for (const 字形& glyph : dictionary.glyphs) {
        搜索单个字形(region, glyph, similarity, items);
        if (items->size() >= kMaxFontMatches) break;
    }
    整理识字项(items);
    return true;
}

/** 构造结构化识字 JSON；text 按行拼接，items 保留每个字形的原图坐标。 */
std::string 识字项转JSON(const std::vector<识字项>& items) {
    std::vector<JsonValue> jsonItems;
    std::map<std::string, JsonValue> item;
    std::string text;
    int previousY = -1;
    int previousHeight = 0;
    for (const 识字项& value : items) {
        if (previousY >= 0 && std::abs(value.y - previousY) > std::max(4, previousHeight / 2)) {
            text.push_back('\n');
        }
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

/** 把完整识字结果按行拼接，在行内查找目标文字并返回每次命中的首个字形坐标。 */
std::vector<字库坐标> 从识字结果查找文字(
        const std::vector<识字项>& items,
        const std::string& target
) {
    struct 行片段 {
        size_t start = 0;
        size_t end = 0;
        const 识字项* item = nullptr;
    };
    struct 文字行 {
        std::string text;
        std::vector<行片段> segments;
        int y = 0;
        int height = 0;
    };

    std::vector<文字行> lines;
    for (const 识字项& item : items) {
        文字行* line = nullptr;
        if (!lines.empty()) {
            int tolerance = std::max(4, std::min(lines.back().height, item.height) / 2);
            if (std::abs(lines.back().y - item.y) <= tolerance) line = &lines.back();
        }
        if (line == nullptr) {
            lines.push_back({});
            line = &lines.back();
            line->y = item.y;
            line->height = item.height;
        }
        size_t start = line->text.size();
        line->text += item.text;
        line->segments.push_back({start, line->text.size(), &item});
        line->height = std::max(line->height, item.height);
    }

    std::vector<字库坐标> found;
    for (const 文字行& line : lines) {
        size_t start = 0;
        while (start <= line.text.size()) {
            size_t position = line.text.find(target, start);
            if (position == std::string::npos) break;
            for (const 行片段& segment : line.segments) {
                if (position >= segment.start && position < segment.end) {
                    found.push_back({segment.item->x, segment.item->y});
                    break;
                }
            }
            start = position + 1;
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

/**
 * 把目标 UTF-8 文本拆成字库标签，优先使用更长标签；同名的多个字形都保留为候选模板。
 */
bool 拆分目标字形(
        const 字库& dictionary,
        const std::string& text,
        std::vector<std::vector<size_t>>* glyphGroups
) {
    if (glyphGroups == nullptr) return 设置字库错误("目标字形输出对象为空");
    glyphGroups->clear();
    size_t offset = 0;
    while (offset < text.size()) {
        const std::string* selected = nullptr;
        for (const std::string& label : dictionary.labelsLongestFirst) {
            if (text.compare(offset, label.size(), label) == 0) {
                selected = &label;
                break;
            }
        }
        if (selected == nullptr) return 设置字库错误("当前字库没有目标文字：" + text.substr(offset));
        auto iterator = dictionary.glyphIndicesByLabel.find(*selected);
        if (iterator == dictionary.glyphIndicesByLabel.end()) {
            return 设置字库错误("当前字库没有目标文字：" + text.substr(offset));
        }
        glyphGroups->push_back(iterator->second);
        offset += selected->size();
    }
    return true;
}

/**
 * 快速找字只搜索目标标签涉及的字形，再按同行和正常字间距连接候选。
 *
 * 与完整 findStr 不同，它不会识别目标之外的字形，因此相邻的无关小字可能被跳过；这是换取
 * 大字库固定文本查找速度的明确语义。
 */
std::vector<字库坐标> 快速查找目标文字(
        const 二值区域& region,
        const 字库& dictionary,
        const std::string& target,
        double similarity
) {
    std::vector<字库坐标> found;
    std::vector<std::vector<size_t>> glyphGroups;
    if (!拆分目标字形(dictionary, target, &glyphGroups)) return found;

    std::vector<std::vector<识字项>> candidates(glyphGroups.size());
    for (size_t groupIndex = 0; groupIndex < glyphGroups.size(); ++groupIndex) {
        for (size_t glyphIndex : glyphGroups[groupIndex]) {
            搜索单个字形(region, dictionary.glyphs[glyphIndex], similarity, &candidates[groupIndex]);
        }
        整理识字项(&candidates[groupIndex]);
        if (candidates[groupIndex].empty()) return found;
    }

    for (const 识字项& first : candidates.front()) {
        const 识字项* previous = &first;
        bool matched = true;
        for (size_t index = 1; index < candidates.size(); ++index) {
            const 识字项* next = nullptr;
            int maximumGap = std::max(4, previous->height / 2);
            for (const 识字项& candidate : candidates[index]) {
                int gap = candidate.x - (previous->x + previous->width);
                int lineTolerance = std::max(3, std::min(previous->height, candidate.height) / 3);
                if (gap >= -1 && gap <= maximumGap
                        && std::abs(candidate.y - previous->y) <= lineTolerance
                        && (next == nullptr || candidate.x < next->x)) {
                    next = &candidate;
                }
            }
            if (next == nullptr) {
                matched = false;
                break;
            }
            previous = next;
        }
        if (matched) found.push_back({first.x, first.y});
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

bool 校验识别参数(double similarity, const char* color, 颜色规则* rule) {
    if (!std::isfinite(similarity) || similarity <= 0.0 || similarity > 1.0) {
        return 设置字库错误("字库相似度必须大于 0 且不超过 1");
    }
    if (!解析颜色规则(color, rule)) {
        return 设置字库错误("字形颜色格式无效，应为 FFFFFF-101010");
    }
    return true;
}

/** 把坐标数组编码为 C ABI 和语言绑定都能稳定读取的 JSON。 */
std::string 坐标转JSON(const std::vector<字库坐标>& points) {
    std::vector<JsonValue> values;
    values.reserve(points.size());
    for (const 字库坐标& point : points) {
        std::map<std::string, JsonValue> item;
        item["x"] = JsonValue::makeNumber(point.x);
        item["y"] = JsonValue::makeNumber(point.y);
        values.push_back(JsonValue::makeObject(std::move(item)));
    }
    return jsonValueToString(JsonValue::makeArray(std::move(values)));
}

} // namespace

bool 设置字库(int index, const char* dictionary) {
    gFontLastError.clear();
    if (!校验字库索引(index)) return false;
    std::string text;
    if (!读取字库文本(dictionary, &text)) return false;
    std::vector<字形> glyphs;
    if (!解析字库文本(text, &glyphs)) return false;
    auto target = std::make_shared<字库>();
    target->glyphs = std::move(glyphs);
    if (!构建字库特征(target.get())) return false;
    {
        std::lock_guard<std::mutex> lock(gDictionaryMutex);
        gDictionaries[index] = std::move(target);
    }
    gFontLastError.clear();
    return true;
}

bool 追加字库(int index, const char* dictionary) {
    gFontLastError.clear();
    if (!校验字库索引(index)) return false;
    std::string text;
    if (!读取字库文本(dictionary, &text)) return false;
    std::vector<字形> appended;
    if (!解析字库文本(text, &appended)) return false;

    // 追加会重选整套字库的低碰撞特征，因此必须在同一个写锁周期内复制当前快照、重建索引
    // 并替换。加载字库不属于识字热路径；这里优先保证两个线程同时追加时不会丢失其中一次。
    std::lock_guard<std::mutex> lock(gDictionaryMutex);
    auto target = std::make_shared<字库>();
    auto iterator = gDictionaries.find(index);
    if (iterator != gDictionaries.end() && iterator->second != nullptr) {
        target->glyphs = iterator->second->glyphs;
    }
    target->glyphs.insert(
            target->glyphs.end(),
            std::make_move_iterator(appended.begin()),
            std::make_move_iterator(appended.end())
    );
    if (!构建字库特征(target.get())) return false;
    gDictionaries[index] = std::move(target);
    gFontLastError.clear();
    return true;
}

bool 使用字库(int index) {
    gFontLastError.clear();
    if (!校验字库索引(index)) return false;
    {
        std::lock_guard<std::mutex> lock(gDictionaryMutex);
        if (gDictionaries.find(index) == gDictionaries.end()) return 设置字库错误("指定字库尚未加载");
    }
    gActiveDictionaryIndex = index;
    gFontLastError.clear();
    return true;
}

void 清空全部字库() {
    // 识字热路径先取得 shared_ptr 快照再读取。清空全局表不会破坏已经开始的识字操作，
    // 最后一个读取者退出后对应字库内存才会自动释放。
    {
        std::lock_guard<std::mutex> lock(gDictionaryMutex);
        gDictionaries.clear();
    }
    gActiveDictionaryIndex = 0;
    gFontLastError.clear();
}

bool 获取字形点阵(
        int x1,
        int y1,
        int x2,
        int y2,
        const char* color,
        std::string* fontPixel
) {
    gFontLastError.clear();
    if (fontPixel == nullptr) return 设置字库错误("字形点阵输出对象为空");
    xiaoyv::image::ColorRule rule;
    std::string error;
    if (!xiaoyv::image::parseColorRule(color == nullptr ? "" : color, &rule, &error)) {
        return 设置字库错误(error);
    }
    ScreenFrame frame;
    if (!captureScreen(&frame)) return 设置字库错误(screenLastError());
    xiaoyv::image::RgbaImageView image{
            frame.pixels,
            frame.width,
            frame.height,
            frame.width * kRgbaPixelBytes
    };
    // 既有脚本允许反向传入区域坐标；共享核心本身保持严格坐标契约，因此在绑定边界统一排序。
    if (!xiaoyv::image::makeFontPixel(
            image,
            std::min(x1, x2),
            std::min(y1, y2),
            std::max(x1, x2),
            std::max(y1, y2),
            rule,
            fontPixel,
            &error)) {
        return 设置字库错误(error);
    }
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
    gFontLastError.clear();
    if (resultJson == nullptr) return 设置字库错误("识字 JSON 输出对象为空");
    颜色规则 rule;
    if (!校验识别参数(similarity, color, &rule)) return false;
    std::shared_ptr<字库> dictionary = 当前字库();
    if (dictionary == nullptr || dictionary->glyphs.empty()) return 设置字库错误("当前线程尚未选择有效字库");
    二值区域 region;
    if (!二值化截图(x1, y1, x2, y2, rule, true, &region)) return false;
    std::vector<识字项> items;
    if (!识别全部字形(region, *dictionary, similarity, &items)) return false;
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
    gFontLastError.clear();
    if (point == nullptr) return 设置字库错误("找字坐标输出对象为空");
    point->x = -1;
    point->y = -1;
    std::string target = text == nullptr ? "" : text;
    if (target.empty()) return 设置字库错误("要查找的文字不能为空");
    颜色规则 rule;
    if (!校验识别参数(similarity, color, &rule)) return false;
    std::shared_ptr<字库> dictionary = 当前字库();
    if (dictionary == nullptr || dictionary->glyphs.empty()) return 设置字库错误("当前线程尚未选择有效字库");
    二值区域 region;
    if (!二值化截图(x1, y1, x2, y2, rule, true, &region)) return false;
    std::vector<识字项> items;
    if (!识别全部字形(region, *dictionary, similarity, &items)) return false;
    std::vector<字库坐标> found = 从识字结果查找文字(items, target);
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
    gFontLastError.clear();
    if (resultJson == nullptr) return 设置字库错误("找字结果 JSON 输出对象为空");
    std::string target = text == nullptr ? "" : text;
    if (target.empty()) return 设置字库错误("要查找的文字不能为空");
    颜色规则 rule;
    if (!校验识别参数(similarity, color, &rule)) return false;
    std::shared_ptr<字库> dictionary = 当前字库();
    if (dictionary == nullptr || dictionary->glyphs.empty()) return 设置字库错误("当前线程尚未选择有效字库");
    二值区域 region;
    if (!二值化截图(x1, y1, x2, y2, rule, true, &region)) return false;
    std::vector<识字项> items;
    if (!识别全部字形(region, *dictionary, similarity, &items)) return false;
    *resultJson = 坐标转JSON(从识字结果查找文字(items, target));
    gFontLastError.clear();
    return true;
}

bool 点阵快速找字(
        int x1,
        int y1,
        int x2,
        int y2,
        const char* text,
        const char* color,
        double similarity,
        字库坐标* point
) {
    gFontLastError.clear();
    if (point == nullptr) return 设置字库错误("快速找字坐标输出对象为空");
    point->x = -1;
    point->y = -1;
    std::string target = text == nullptr ? "" : text;
    if (target.empty()) return 设置字库错误("要快速查找的文字不能为空");
    颜色规则 rule;
    if (!校验识别参数(similarity, color, &rule)) return false;
    std::shared_ptr<字库> dictionary = 当前字库();
    if (dictionary == nullptr || dictionary->glyphs.empty()) return 设置字库错误("当前线程尚未选择有效字库");
    二值区域 region;
    if (!二值化截图(x1, y1, x2, y2, rule, true, &region)) return false;
    std::vector<字库坐标> found = 快速查找目标文字(region, *dictionary, target, similarity);
    if (!gFontLastError.empty()) return false;
    if (found.empty()) {
        gFontLastError.clear();
        return false;
    }
    *point = found.front();
    gFontLastError.clear();
    return true;
}

bool 点阵快速找字全部(
        int x1,
        int y1,
        int x2,
        int y2,
        const char* text,
        const char* color,
        double similarity,
        std::string* resultJson
) {
    gFontLastError.clear();
    if (resultJson == nullptr) return 设置字库错误("快速找字结果 JSON 输出对象为空");
    std::string target = text == nullptr ? "" : text;
    if (target.empty()) return 设置字库错误("要快速查找的文字不能为空");
    颜色规则 rule;
    if (!校验识别参数(similarity, color, &rule)) return false;
    std::shared_ptr<字库> dictionary = 当前字库();
    if (dictionary == nullptr || dictionary->glyphs.empty()) return 设置字库错误("当前线程尚未选择有效字库");
    二值区域 region;
    if (!二值化截图(x1, y1, x2, y2, rule, true, &region)) return false;
    std::vector<字库坐标> found = 快速查找目标文字(region, *dictionary, target, similarity);
    if (!gFontLastError.empty()) return false;
    *resultJson = 坐标转JSON(found);
    gFontLastError.clear();
    return true;
}

std::string 取字库错误() {
    return gFontLastError;
}

} // namespace xiaoyv::api
