/**
 * 文件用途：实现 native 图片句柄池，用于截图像素缓存、取色和释放。
 */
#include "image_store.h"

#include <map>
#include <mutex>

namespace {

struct StoredImage {
    int id = 0;
    ImageFrame frame;
};

std::mutex gImageMutex;
std::map<int, StoredImage> gImages;
int gNextImageId = 1;

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

ImageMetadata storeImageFrame(ImageFrame frame) {
    std::lock_guard<std::mutex> lock(gImageMutex);

    StoredImage image;
    image.id = gNextImageId++;
    image.frame = std::move(frame);

    ImageMetadata metadata = makeMetadataLocked(image);
    gImages[image.id] = std::move(image);
    return metadata;
}

bool releaseImageFrame(int imageId) {
    std::lock_guard<std::mutex> lock(gImageMutex);
    return gImages.erase(imageId) > 0;
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
