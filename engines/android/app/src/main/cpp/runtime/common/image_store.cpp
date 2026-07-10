/**
 * 文件用途：实现 native 图片句柄池和屏幕帧缓存，用于截图复用和取色。
 */
#include "image_store.h"

#include <chrono>
#include <map>
#include <mutex>
#include <utility>

namespace {

struct StoredImage {
    int id = 0;
    ImageFrame frame;
};

constexpr long long kScreenFrameCacheMs = 20;

std::mutex gImageMutex;
std::map<int, StoredImage> gImages;
int gNextImageId = 1;
int gScreenFrameImageId = 0;
long long gScreenFrameStoredAtMs = 0;
long long gScreenFrameCacheMs = kScreenFrameCacheMs;
bool gKeepScreenFrame = false;

long long steadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

ImageMetadata makeMetadataLocked(const StoredImage& image) {
    ImageMetadata metadata;
    metadata.id = image.id;
    metadata.width = image.frame.width;
    metadata.height = image.frame.height;
    metadata.rowStride = image.frame.rowStride;
    metadata.pixelStride = image.frame.pixelStride;
    metadata.byteLength = image.frame.pixels.size();
    metadata.format = image.frame.format;
    metadata.source = image.frame.source;
    metadata.captureDurationMs = image.frame.captureDurationMs;
    return metadata;
}

bool readPixelFromFrameLocked(
        const ImageFrame& frame,
        int x,
        int y,
        PixelColor* color,
        std::string* error) {
    if (x < 0 || y < 0 || x >= frame.width || y >= frame.height) {
        *error = "pixel coordinate is out of image bounds";
        return false;
    }

    if (frame.pixelStride < 4 || frame.rowStride <= 0) {
        *error = "image pixel layout is unsupported";
        return false;
    }

    // 图片写入句柄时已经压成紧凑 RGBA。这里直接计算内存偏移，
    // 不做编码、不落盘，也不重新跨 Java 层取图。
    size_t offset = static_cast<size_t>(y) * static_cast<size_t>(frame.rowStride)
            + static_cast<size_t>(x) * static_cast<size_t>(frame.pixelStride);
    if (offset + 3 >= frame.pixels.size()) {
        *error = "image pixel data is incomplete";
        return false;
    }

    color->red = frame.pixels[offset];
    color->green = frame.pixels[offset + 1];
    color->blue = frame.pixels[offset + 2];
    color->alpha = frame.pixels[offset + 3];
    color->rgb = (color->red << 16) | (color->green << 8) | color->blue;
    return true;
}

} // namespace

bool getCachedScreenFrame(ImageMetadata* metadata) {
    std::lock_guard<std::mutex> lock(gImageMutex);

    if (gScreenFrameImageId <= 0) {
        return false;
    }

    auto iterator = gImages.find(gScreenFrameImageId);
    if (iterator == gImages.end()) {
        gScreenFrameImageId = 0;
        gScreenFrameStoredAtMs = 0;
        return false;
    }

    // keepCapture 开启后，当前屏幕帧会一直复用，直到脚本调用
    // releaseCapture、脚本结束或新脚本开始清空缓存。
    if (gKeepScreenFrame) {
        *metadata = makeMetadataLocked(iterator->second);
        return true;
    }

    // 缓冲时间内的连续截图请求都视为同一屏幕帧，直接复用 native 内存里的
    // 当前截图句柄，避免重复拉取屏幕像素。
    long long ageMs = steadyNowMs() - gScreenFrameStoredAtMs;
    if (ageMs < 0 || ageMs > gScreenFrameCacheMs) {
        return false;
    }

    *metadata = makeMetadataLocked(iterator->second);
    return true;
}

ImageMetadata storeScreenFrame(ImageFrame frame) {
    std::lock_guard<std::mutex> lock(gImageMutex);

    if (gScreenFrameImageId > 0) {
        gImages.erase(gScreenFrameImageId);
    }

    StoredImage image;
    image.id = gNextImageId++;
    image.frame = std::move(frame);

    ImageMetadata metadata = makeMetadataLocked(image);
    gScreenFrameImageId = image.id;
    gScreenFrameStoredAtMs = steadyNowMs();
    gImages[image.id] = std::move(image);
    return metadata;
}

void keepScreenFrameCache() {
    std::lock_guard<std::mutex> lock(gImageMutex);
    gKeepScreenFrame = true;
}

void releaseScreenFrameCache() {
    std::lock_guard<std::mutex> lock(gImageMutex);
    gKeepScreenFrame = false;
}

bool setScreenFrameCacheDurationMs(long long durationMs) {
    if (durationMs < 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(gImageMutex);
    gScreenFrameCacheMs = durationMs;
    return true;
}

void clearScreenFrameCache() {
    std::lock_guard<std::mutex> lock(gImageMutex);

    if (gScreenFrameImageId > 0) {
        gImages.erase(gScreenFrameImageId);
    }
    gScreenFrameImageId = 0;
    gScreenFrameStoredAtMs = 0;
    gKeepScreenFrame = false;
    gScreenFrameCacheMs = kScreenFrameCacheMs;
}

bool readImagePixel(int imageId, int x, int y, PixelColor* color, std::string* error) {
    std::lock_guard<std::mutex> lock(gImageMutex);

    auto iterator = gImages.find(imageId);
    if (iterator == gImages.end()) {
        *error = "image handle is not found";
        return false;
    }

    return readPixelFromFrameLocked(iterator->second.frame, x, y, color, error);
}

bool readImagePixels(
        int imageId,
        const std::vector<PixelPoint>& points,
        std::vector<int>* colors,
        std::string* error) {
    std::lock_guard<std::mutex> lock(gImageMutex);

    auto iterator = gImages.find(imageId);
    if (iterator == gImages.end()) {
        *error = "image handle is not found";
        return false;
    }

    colors->clear();
    colors->reserve(points.size());

    const ImageFrame& frame = iterator->second.frame;
    for (size_t i = 0; i < points.size(); ++i) {
        PixelColor color;
        std::string pixelError;
        if (!readPixelFromFrameLocked(frame, points[i].x, points[i].y, &color, &pixelError)) {
            *error = "point #" + std::to_string(i + 1) + " failed: " + pixelError;
            colors->clear();
            return false;
        }

        colors->push_back(color.rgb);
    }

    return true;
}
