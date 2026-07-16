/**
 * 文件用途：实现模板找图与截图保存，图片格式解码由 AndroidBridge 完成，匹配热路径在 C++ 内执行。
 */
#include "image_api.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unordered_map>
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
// 模板缓存只服务当前脚本任务。长时间脚本按已分配点阵容量限制为 5 MiB，超限时淘汰
// 最久未使用模板；单个模板超过上限时仍可完成当前找图，但不会进入缓存。
constexpr size_t kDefaultTemplateCacheBytes = 5u * 1024u * 1024u;

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
    std::vector<模板像素> pixels;
    std::vector<模板像素> anchors;
};

/** 模板缓存项。shared_ptr 允许清缓存时，正在执行的 findPic 安全完成本次匹配。 */
struct 模板缓存项 {
    std::shared_ptr<const 模板图片> image;
    size_t memoryBytes = 0;
    std::uint64_t lastAccess = 0;
};

using 模板缓存表 = std::unordered_map<std::string, 模板缓存项>;

std::mutex gTemplateMutex;
模板缓存表 gTemplates;
size_t gTemplateCacheBytes = 0;
size_t gTemplateCacheMaxBytes = kDefaultTemplateCacheBytes;
std::uint64_t gTemplateAccessSequence = 0;
thread_local std::string gImageLastError;

/**
 * 按容器实际已分配容量统计一个缓存项。
 *
 * vector 使用 capacity 而不是 size，才能把预留但尚未使用的内存计入；标准库哈希节点与
 * shared_ptr 控制块没有可移植的精确查询接口，因此额外计入对应对象本身和缓存键字节。
 */
size_t 计算模板缓存字节(const std::string& cacheKey, const 模板图片& image) {
    return sizeof(模板缓存项)
            + sizeof(模板图片)
            + sizeof(std::string)
            + cacheKey.size() + 1
            + image.pixels.capacity() * sizeof(模板像素)
            + image.anchors.capacity() * sizeof(模板像素);
}

/** 删除一个缓存项并同步维护总内存。调用前必须持有 gTemplateMutex。 */
void 删除模板缓存项(模板缓存表::iterator iterator) {
    if (iterator == gTemplates.end()) {
        return;
    }
    gTemplateCacheBytes = iterator->second.memoryBytes > gTemplateCacheBytes
            ? 0
            : gTemplateCacheBytes - iterator->second.memoryBytes;
    gTemplates.erase(iterator);
}

/**
 * 读取缓存并更新最近访问顺序。
 *
 * checkSourceStamp 只用于普通文件；文件时间变化时立即移除旧点阵，随后由调用方重新解码。
 */
std::shared_ptr<const 模板图片> 读取模板缓存(
        const std::string& cacheKey,
        bool checkSourceStamp,
        long long sourceStamp
) {
    std::lock_guard<std::mutex> lock(gTemplateMutex);
    auto iterator = gTemplates.find(cacheKey);
    if (iterator == gTemplates.end()) {
        return nullptr;
    }
    if (checkSourceStamp && (sourceStamp == 0 || iterator->second.image->sourceStamp != sourceStamp)) {
        删除模板缓存项(iterator);
        return nullptr;
    }
    iterator->second.lastAccess = ++gTemplateAccessSequence;
    return iterator->second.image;
}

/** 按最近访问顺序淘汰到当前内存上限；调用前必须持有 gTemplateMutex。 */
void 淘汰超限模板() {
    while (gTemplateCacheBytes > gTemplateCacheMaxBytes && !gTemplates.empty()) {
        auto oldest = gTemplates.begin();
        for (auto iterator = std::next(gTemplates.begin()); iterator != gTemplates.end(); ++iterator) {
            if (iterator->second.lastAccess < oldest->second.lastAccess) {
                oldest = iterator;
            }
        }
        删除模板缓存项(oldest);
    }
}

/**
 * 写入模板缓存并按总内存执行 LRU 淘汰。
 *
 * 淘汰只在新模板进入缓存或主动缩小上限时发生；缓存命中的热路径只更新访问序号。
 */
void 写入模板缓存(
        const std::string& cacheKey,
        const std::shared_ptr<const 模板图片>& image
) {
    if (image == nullptr) {
        return;
    }
    size_t memoryBytes = 计算模板缓存字节(cacheKey, *image);

    std::lock_guard<std::mutex> lock(gTemplateMutex);
    if (gTemplateCacheMaxBytes == 0 || memoryBytes > gTemplateCacheMaxBytes) {
        return;
    }
    auto previous = gTemplates.find(cacheKey);
    if (previous != gTemplates.end()) {
        删除模板缓存项(previous);
    }

    模板缓存项 entry;
    entry.image = image;
    entry.memoryBytes = memoryBytes;
    entry.lastAccess = ++gTemplateAccessSequence;
    gTemplateCacheBytes += memoryBytes;
    gTemplates.emplace(cacheKey, std::move(entry));
    淘汰超限模板();
}

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

/** 把普通文件模板的相对路径解析到当前脚本工作目录，供加载和精确清理共用。 */
std::string 解析普通图片路径(const std::string& name) {
    if (是绝对图片路径(name)) {
        return name;
    }
    std::string workPath = runtimeScriptWorkPath();
    if (workPath.empty()) {
        return name;
    }
    if (workPath.back() != '/') {
        workPath.push_back('/');
    }
    return workPath + name;
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
bool 构建模板(const AndroidImageDecodeResult& decoded, 模板图片* output) {
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
 * 普通文件缓存会用 stat 修改时间检测变更；ALPKG 资源只在当前脚本任务内缓存。返回
 * shared_ptr，避免每次 findPic 命中缓存时复制整份模板点阵。
 */
bool 获取模板(const std::string& picName, std::shared_ptr<const 模板图片>* output) {
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
        // ALPKG 模板缓存只活到当前脚本结束，因此资源相对路径在任务内已经是唯一键。
        // 先查缓存再读 ZIP，避免高频 findPic 反复读取同一资源字节。
        cacheKey = "alpkg:" + name;
        std::shared_ptr<const 模板图片> cached = 读取模板缓存(cacheKey, false, 0);
        if (cached != nullptr) {
            *output = std::move(cached);
            return true;
        }

        std::vector<unsigned char> resourceBytes;
        std::string resourceError;
        if (readActiveAlpkgResource(name, &resourceBytes, &resourceError)) {
            packageResource = true;
            decoded = AndroidBridge::decodeImageBytes(resourceBytes.data(), resourceBytes.size());
        }
    }

    if (!packageResource) {
        std::string path = 解析普通图片路径(name);
        cacheKey = "file:" + path;
        long long fileStamp = 获取文件时间戳(path);
        std::shared_ptr<const 模板图片> cached = 读取模板缓存(cacheKey, true, fileStamp);
        if (cached != nullptr) {
            *output = std::move(cached);
            return true;
        }
        decoded = AndroidBridge::decodeImageFile(path);
        if (decoded.success && fileStamp != 0) {
            decoded.sourceStamp = fileStamp;
        }
    }

    auto templateImage = std::make_shared<模板图片>();
    if (!构建模板(decoded, templateImage.get())) {
        return false;
    }

    写入模板缓存(cacheKey, templateImage);
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

    std::shared_ptr<const 模板图片> templateImage;
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
    int right = std::min(frame.width - templateImage->width, std::max(x1, x2) - templateImage->width + 1);
    int bottom = std::min(frame.height - templateImage->height, std::max(y1, y2) - templateImage->height + 1);
    if (left > right || top > bottom) {
        return 设置图片错误("找图区域不足以容纳模板图片");
    }

    bool found = 按方向扫描(left, top, right, bottom, direction, [&](int x, int y) {
        if (!匹配候选位置(
                frame.pixels,
                frame.width,
                x,
                y,
                *templateImage,
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

bool 保存当前截图(const char* path, const 截图区域* region) {
    std::string outputPath = 去空白(path == nullptr ? "" : path);
    if (outputPath.empty()) {
        return 设置图片错误("截图保存路径不能为空");
    }

    ScreenFrame frame;
    if (!captureScreen(&frame)) {
        return 设置图片错误(screenLastError());
    }
    if (frame.pixels == nullptr || frame.width <= 0 || frame.height <= 0) {
        return 设置图片错误("当前截图点阵无效");
    }

    int left = 0;
    int top = 0;
    int right = frame.width;
    int bottom = frame.height;
    if (region != nullptr) {
        left = region->left;
        top = region->top;
        right = region->right;
        bottom = region->bottom;
        if (left < 0 || top < 0 || right > frame.width || bottom > frame.height) {
            return 设置图片错误("截图区域超出屏幕范围");
        }
        if (left >= right || top >= bottom) {
            return 设置图片错误("截图区域必须满足 left < right 且 top < bottom");
        }
    }

    size_t bytes = static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height) * kRgbaPixelBytes;
    if (!AndroidBridge::saveRgbaImage(
            frame.pixels,
            frame.width,
            frame.height,
            bytes,
            left,
            top,
            right,
            bottom,
            outputPath
    )) {
        return 设置图片错误("保存截图失败：" + outputPath);
    }
    gImageLastError.clear();
    return true;
}

void 清理图片缓存(const char* picName) {
    std::string name = 去空白(picName == nullptr ? "" : picName);
    std::string packageKey;
    std::string fileKey;
    if (!name.empty()) {
        if (!是绝对图片路径(name)) {
            packageKey = "alpkg:" + name;
        }
        // 路径解析会读取脚本工作目录，必须在取得模板缓存锁之前完成，保持固定锁顺序。
        fileKey = "file:" + 解析普通图片路径(name);
    }

    std::lock_guard<std::mutex> lock(gTemplateMutex);
    if (name.empty()) {
        gTemplates.clear();
        gTemplateCacheBytes = 0;
        gTemplateAccessSequence = 0;
        return;
    }

    // 一个相对路径在 ALPKG 中可能是包资源，也可能回退到脚本目录普通文件；分别按与加载时
    // 完全相同的键精确删除，不能用子串匹配误删其他同名模板。
    if (!packageKey.empty()) {
        auto packageIterator = gTemplates.find(packageKey);
        删除模板缓存项(packageIterator);
    }
    auto fileIterator = gTemplates.find(fileKey);
    删除模板缓存项(fileIterator);
}

void 设置图片缓存上限(std::size_t maxBytes) {
    std::lock_guard<std::mutex> lock(gTemplateMutex);
    gTemplateCacheMaxBytes = maxBytes;
    淘汰超限模板();
}

void 重置图片缓存() {
    std::lock_guard<std::mutex> lock(gTemplateMutex);
    gTemplates.clear();
    gTemplateCacheBytes = 0;
    gTemplateCacheMaxBytes = kDefaultTemplateCacheBytes;
    gTemplateAccessSequence = 0;
}

std::string 取图片错误() {
    return gImageLastError;
}

} // namespace xiaoyv::api
