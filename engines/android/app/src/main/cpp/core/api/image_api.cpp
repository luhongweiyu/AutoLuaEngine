/**
 * 文件用途：实现模板找图与截图保存，图片格式解码由 AndroidBridge 完成，匹配热路径在 C++ 内执行。
 */
#include "image_api.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <utility>
#include <vector>

#include "package_api.h"
#include "runtime_api.h"
#include "screen_api.h"
#include "../../platform/android_bridge.h"

namespace xiaoyv::api {
namespace {

constexpr int kRgbaPixelBytes = 4;
constexpr int kMaxTemplatePixels = 16 * 1024 * 1024;
constexpr int kMaxAnchorCount = 64;

/** 模板中一个需要比较的非透明像素。 */
struct 模板像素 {
    int x = 0;
    int y = 0;
    unsigned char r = 0;
    unsigned char g = 0;
    unsigned char b = 0;
};

/** 已经由 Java 解码并预处理完成的模板。 */
struct 模板图片 {
    int width = 0;
    int height = 0;
    long long sourceStamp = 0;
    bool isPackageResource = false;
    std::vector<模板像素> pixels;
    std::vector<模板像素> anchors;
};

std::mutex gTemplateMutex;
std::map<std::string, 模板图片> gTemplates;
thread_local std::string gImageLastError;

/** 设置当前线程错误并统一返回 false。 */
bool 设置图片错误(const std::string& error) {
    gImageLastError = error;
    return false;
}

/** 去掉图片路径前后的空白，脚本参数常来自文本拼接，不能把空格误当作文件名一部分。 */
std::string 去空白(std::string value) {
    size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

/** 判断 Android 文件路径是否是绝对路径或 file:// URI。 */
bool 是绝对图片路径(const std::string& path) {
    return !path.empty() && (path[0] == '/' || path.rfind("file://", 0) == 0);
}

/** 取得普通文件的低成本修改时间。不存在时返回 0，由 Java 解码入口给出具体错误。 */
long long 获取文件时间戳(const std::string& path) {
    std::string filePath = path.rfind("file://", 0) == 0 ? path.substr(7) : path;
    struct stat info {};
    if (stat(filePath.c_str(), &info) != 0) {
        return 0;
    }
#if defined(__ANDROID_API__) && __ANDROID_API__ >= 21
    return static_cast<long long>(info.st_mtim.tv_sec) * 1000LL
            + static_cast<long long>(info.st_mtim.tv_nsec / 1000000L);
#else
    return static_cast<long long>(info.st_mtime) * 1000LL;
#endif
}

/**
 * 把 Java 返回的 RGBA 图片转成模板比较项，并挑选均匀分布的锚点加速绝大多数不命中位置。
 */
bool 构建模板(const AndroidImageDecodeResult& decoded, bool isPackageResource, 模板图片* output) {
    if (output == nullptr) {
        return 设置图片错误("模板图片输出对象为空");
    }
    if (!decoded.success || decoded.width <= 0 || decoded.height <= 0) {
        return 设置图片错误(decoded.error.empty() ? "模板图片解码失败" : decoded.error);
    }

    long long pixelCount = static_cast<long long>(decoded.width) * static_cast<long long>(decoded.height);
    if (pixelCount <= 0 || pixelCount > kMaxTemplatePixels
            || decoded.pixels.size() != static_cast<size_t>(pixelCount * kRgbaPixelBytes)) {
        return 设置图片错误("模板图片尺寸或 RGBA 点阵无效");
    }

    模板图片 templateImage;
    templateImage.width = decoded.width;
    templateImage.height = decoded.height;
    templateImage.sourceStamp = decoded.sourceStamp;
    templateImage.isPackageResource = isPackageResource;
    templateImage.pixels.reserve(static_cast<size_t>(pixelCount));

    for (int y = 0; y < decoded.height; ++y) {
        for (int x = 0; x < decoded.width; ++x) {
            const unsigned char* rgba = decoded.pixels.data()
                    + (static_cast<size_t>(y) * static_cast<size_t>(decoded.width)
                            + static_cast<size_t>(x)) * kRgbaPixelBytes;
            // 透明区域不参与匹配，使 PNG 模板可以自然保留不规则轮廓。
            if (rgba[3] < 16) {
                continue;
            }
            templateImage.pixels.push_back({x, y, rgba[0], rgba[1], rgba[2]});
        }
    }

    if (templateImage.pixels.empty()) {
        return 设置图片错误("模板图片没有可匹配的非透明像素");
    }

    const size_t step = std::max<size_t>(
            1,
            (templateImage.pixels.size() + kMaxAnchorCount - 1) / kMaxAnchorCount
    );
    for (size_t index = 0; index < templateImage.pixels.size(); index += step) {
        templateImage.anchors.push_back(templateImage.pixels[index]);
    }
    if (templateImage.anchors.back().x != templateImage.pixels.back().x
            || templateImage.anchors.back().y != templateImage.pixels.back().y) {
        templateImage.anchors.push_back(templateImage.pixels.back());
    }

    *output = std::move(templateImage);
    return true;
}

/**
 * 解析图片色差参数。
 *
 * DaMo/懒人常把 delta_color 写成 "101010"；为了让脚本迁移更宽松，这里也接受 0x 前缀、
 * 逗号和空白。最终每个通道都是独立容差。
 */
bool 解析色差(const char* text, int* red, int* green, int* blue) {
    if (red == nullptr || green == nullptr || blue == nullptr) {
        return false;
    }
    std::string value = 去空白(text == nullptr ? "" : text);
    if (value.empty()) {
        *red = 0;
        *green = 0;
        *blue = 0;
        return true;
    }
    if (value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0) {
        value.erase(0, 2);
    }
    value.erase(std::remove_if(value.begin(), value.end(), [](char character) {
        return character == ',' || character == '|' || character == ' ' || character == '\t';
    }), value.end());
    if (value.size() != 6) {
        return false;
    }

    auto hexValue = [](char character) -> int {
        if (character >= '0' && character <= '9') {
            return character - '0';
        }
        if (character >= 'a' && character <= 'f') {
            return character - 'a' + 10;
        }
        if (character >= 'A' && character <= 'F') {
            return character - 'A' + 10;
        }
        return -1;
    };
    int bytes[3] = {0, 0, 0};
    for (int index = 0; index < 3; ++index) {
        int high = hexValue(value[static_cast<size_t>(index * 2)]);
        int low = hexValue(value[static_cast<size_t>(index * 2 + 1)]);
        if (high < 0 || low < 0) {
            return false;
        }
        bytes[index] = high * 16 + low;
    }
    *red = bytes[0];
    *green = bytes[1];
    *blue = bytes[2];
    return true;
}

/** 单个模板像素与当前截图像素是否满足 RGB 容差。 */
inline bool 像素匹配(
        const unsigned char* screenPixels,
        int screenWidth,
        int baseX,
        int baseY,
        const 模板像素& pixel,
        int redDelta,
        int greenDelta,
        int blueDelta
) {
    const unsigned char* current = screenPixels
            + (static_cast<size_t>(baseY + pixel.y) * static_cast<size_t>(screenWidth)
                    + static_cast<size_t>(baseX + pixel.x)) * kRgbaPixelBytes;
    return std::abs(static_cast<int>(current[0]) - static_cast<int>(pixel.r)) <= redDelta
            && std::abs(static_cast<int>(current[1]) - static_cast<int>(pixel.g)) <= greenDelta
            && std::abs(static_cast<int>(current[2]) - static_cast<int>(pixel.b)) <= blueDelta;
}

/** 在一个候选坐标执行两段式匹配：少量锚点快速排除，剩余像素按相似度阈值精确确认。 */
bool 匹配候选位置(
        const unsigned char* screenPixels,
        int screenWidth,
        int baseX,
        int baseY,
        const 模板图片& templateImage,
        int redDelta,
        int greenDelta,
        int blueDelta,
        double similarity
) {
    const int requiredAnchors = static_cast<int>(std::ceil(
            similarity * static_cast<double>(templateImage.anchors.size())
    ));
    int anchorMatches = 0;
    for (size_t index = 0; index < templateImage.anchors.size(); ++index) {
        if (像素匹配(
                screenPixels,
                screenWidth,
                baseX,
                baseY,
                templateImage.anchors[index],
                redDelta,
                greenDelta,
                blueDelta
        )) {
            ++anchorMatches;
        }
        int remaining = static_cast<int>(templateImage.anchors.size() - index - 1);
        if (anchorMatches + remaining < requiredAnchors) {
            return false;
        }
    }

    const int requiredPixels = static_cast<int>(std::ceil(
            similarity * static_cast<double>(templateImage.pixels.size())
    ));
    int matchedPixels = 0;
    for (size_t index = 0; index < templateImage.pixels.size(); ++index) {
        if (像素匹配(
                screenPixels,
                screenWidth,
                baseX,
                baseY,
                templateImage.pixels[index],
                redDelta,
                greenDelta,
                blueDelta
        )) {
            ++matchedPixels;
        }
        int remaining = static_cast<int>(templateImage.pixels.size() - index - 1);
        if (matchedPixels + remaining < requiredPixels) {
            return false;
        }
    }
    return matchedPixels >= requiredPixels;
}

/**
 * 根据找色既有 1..8 扫描约定枚举候选坐标。
 *
 * 这里直接把模板左上角作为命中坐标；x2/y2 均是原始搜索矩形右下角，候选范围已经在调用前
 * 预先裁剪为模板完整落在区域内的坐标。
 */
template<typename Visitor>
bool 按方向扫描(int x1, int y1, int x2, int y2, int direction, Visitor&& visitor) {
    switch (direction) {
        case 1:
            for (int x = x1; x <= x2; ++x) {
                for (int y = y1; y <= y2; ++y) {
                    if (visitor(x, y)) return true;
                }
            }
            return false;
        case 2:
            for (int y = y1; y <= y2; ++y) {
                for (int x = x1; x <= x2; ++x) {
                    if (visitor(x, y)) return true;
                }
            }
            return false;
        case 3:
            for (int y = y1; y <= y2; ++y) {
                for (int x = x2; x >= x1; --x) {
                    if (visitor(x, y)) return true;
                }
            }
            return false;
        case 4:
            for (int x = x2; x >= x1; --x) {
                for (int y = y1; y <= y2; ++y) {
                    if (visitor(x, y)) return true;
                }
            }
            return false;
        case 5:
            for (int x = x2; x >= x1; --x) {
                for (int y = y2; y >= y1; --y) {
                    if (visitor(x, y)) return true;
                }
            }
            return false;
        case 6:
            for (int y = y2; y >= y1; --y) {
                for (int x = x2; x >= x1; --x) {
                    if (visitor(x, y)) return true;
                }
            }
            return false;
        case 7:
            for (int y = y2; y >= y1; --y) {
                for (int x = x1; x <= x2; ++x) {
                    if (visitor(x, y)) return true;
                }
            }
            return false;
        case 8:
            for (int x = x1; x <= x2; ++x) {
                for (int y = y2; y >= y1; --y) {
                    if (visitor(x, y)) return true;
                }
            }
            return false;
        default:
            return false;
    }
}

/**
 * 加载或复用模板。
 *
 * 普通文件缓存会用 stat 修改时间检测变更；ALPKG 资源在同一脚本运行期间保持缓存，避免每次
 * findPic 都重新读取 ZIP 和图片解码。脚本结束后的清理由调用方可显式触发。
 */
bool 获取模板(const std::string& picName, 模板图片* output) {
    if (output == nullptr) {
        return 设置图片错误("模板输出对象为空");
    }
    std::string name = 去空白(picName);
    if (name.empty()) {
        return 设置图片错误("图片名称不能为空");
    }

    bool packageResource = false;
    std::string cacheKey;
    AndroidImageDecodeResult decoded;

    if (!是绝对图片路径(name)) {
        std::vector<unsigned char> resourceBytes;
        std::string resourceError;
        if (readActiveAlpkgResource(name, &resourceBytes, &resourceError)) {
            packageResource = true;
            cacheKey = "alpkg:" + runtimeScriptWorkPath() + ":" + name;
            {
                std::lock_guard<std::mutex> lock(gTemplateMutex);
                auto iterator = gTemplates.find(cacheKey);
                if (iterator != gTemplates.end()) {
                    *output = iterator->second;
                    return true;
                }
            }
            decoded = AndroidBridge::decodeImageBytes(resourceBytes.data(), resourceBytes.size());
        }
    }

    if (!packageResource) {
        std::string path = name;
        if (!是绝对图片路径(path)) {
            std::string workPath = runtimeScriptWorkPath();
            if (!workPath.empty()) {
                if (workPath.back() != '/') {
                    workPath.push_back('/');
                }
                path = workPath + path;
            }
        }
        cacheKey = "file:" + path;
        long long fileStamp = 获取文件时间戳(path);
        {
            std::lock_guard<std::mutex> lock(gTemplateMutex);
            auto iterator = gTemplates.find(cacheKey);
            if (iterator != gTemplates.end() && iterator->second.sourceStamp == fileStamp) {
                *output = iterator->second;
                return true;
            }
        }
        decoded = AndroidBridge::decodeImageFile(path);
        if (decoded.success && fileStamp != 0) {
            decoded.sourceStamp = fileStamp;
        }
    }

    模板图片 templateImage;
    if (!构建模板(decoded, packageResource, &templateImage)) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(gTemplateMutex);
        gTemplates[cacheKey] = templateImage;
    }
    *output = std::move(templateImage);
    return true;
}

} // namespace

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
) {
    if (point == nullptr) {
        return 设置图片错误("找图输出坐标不能为空");
    }
    point->x = -1;
    point->y = -1;
    if (direction < 1 || direction > 8) {
        return 设置图片错误("找图方向必须在 1 到 8 之间");
    }
    if (!std::isfinite(similarity) || similarity <= 0.0 || similarity > 1.0) {
        return 设置图片错误("图片相似度必须大于 0 且不超过 1");
    }

    int redDelta = 0;
    int greenDelta = 0;
    int blueDelta = 0;
    if (!解析色差(deltaColor, &redDelta, &greenDelta, &blueDelta)) {
        return 设置图片错误("图片色差格式无效，应为 6 位十六进制 RGB，例如 101010");
    }

    模板图片 templateImage;
    if (!获取模板(picName == nullptr ? "" : picName, &templateImage)) {
        return false;
    }

    ScreenFrame frame;
    if (!captureScreen(&frame)) {
        return 设置图片错误(screenLastError());
    }
    if (frame.pixels == nullptr || frame.width <= 0 || frame.height <= 0) {
        return 设置图片错误("当前截图点阵无效");
    }

    int left = std::max(0, std::min(x1, x2));
    int top = std::max(0, std::min(y1, y2));
    int right = std::min(frame.width - templateImage.width, std::max(x1, x2) - templateImage.width + 1);
    int bottom = std::min(frame.height - templateImage.height, std::max(y1, y2) - templateImage.height + 1);
    if (left > right || top > bottom) {
        return 设置图片错误("找图区域不足以容纳模板图片");
    }

    bool found = 按方向扫描(left, top, right, bottom, direction, [&](int x, int y) {
        if (!匹配候选位置(
                frame.pixels,
                frame.width,
                x,
                y,
                templateImage,
                redDelta,
                greenDelta,
                blueDelta,
                similarity
        )) {
            return false;
        }
        point->x = x;
        point->y = y;
        return true;
    });

    if (!found) {
        gImageLastError.clear();
        return false;
    }
    gImageLastError.clear();
    return true;
}

bool 保存当前截图(const char* path) {
    std::string outputPath = 去空白(path == nullptr ? "" : path);
    if (outputPath.empty()) {
        return 设置图片错误("截图保存路径不能为空");
    }

    ScreenFrame frame;
    if (!captureScreen(&frame)) {
        return 设置图片错误(screenLastError());
    }
    size_t bytes = static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height) * kRgbaPixelBytes;
    if (!AndroidBridge::saveRgbaImage(frame.pixels, frame.width, frame.height, bytes, outputPath)) {
        return 设置图片错误("保存截图失败：" + outputPath);
    }
    gImageLastError.clear();
    return true;
}

void 清理图片缓存(const char* picName) {
    std::string name = 去空白(picName == nullptr ? "" : picName);
    std::lock_guard<std::mutex> lock(gTemplateMutex);
    if (name.empty()) {
        gTemplates.clear();
        return;
    }
    for (auto iterator = gTemplates.begin(); iterator != gTemplates.end();) {
        if (iterator->first.find(name) != std::string::npos) {
            iterator = gTemplates.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

std::string 取图片错误() {
    return gImageLastError;
}

} // namespace xiaoyv::api
