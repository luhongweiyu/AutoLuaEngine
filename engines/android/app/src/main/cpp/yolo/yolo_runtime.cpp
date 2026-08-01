/**
 * 文件用途：实现 NCNN YOLOv5 CPU 推理与结果 JSON 编码。
 *
 * 预处理、三组 anchor 解码和 Focus 自定义层按 Tencent/ncnn 的 BSD-3-Clause YOLOv5 示例
 * 重新组织为无 OpenCV、可由多语言 C ABI 调用的 RGBA 运行时。模型文件不随 APK 或 ALPKG 分发。
 */
#include "yolo_runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "layer.h"
#include "net.h"

namespace xiaoyv::yolo {
namespace {

constexpr int kChannels = 4;
constexpr int kMaxStride = 64;
constexpr int kExpectedOutputCount = 3;

struct Detection {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    int labelIndex = -1;
    float probability = 0.0F;
};

/** 兼容较早 YOLOv5 NCNN 导出中仍保留的 Focus 图层。 */
class YoloV5Focus final : public ncnn::Layer {
public:
    YoloV5Focus() {
        one_blob_only = true;
    }

    int forward(
            const ncnn::Mat& bottomBlob,
            ncnn::Mat& topBlob,
            const ncnn::Option& options
    ) const override {
        if (bottomBlob.dims != 3 || bottomBlob.w < 2 || bottomBlob.h < 2 || bottomBlob.c <= 0) {
            return -100;
        }

        const int outputWidth = bottomBlob.w / 2;
        const int outputHeight = bottomBlob.h / 2;
        const int outputChannels = bottomBlob.c * 4;
        topBlob.create(outputWidth, outputHeight, outputChannels, 4U, 1, options.blob_allocator);
        if (topBlob.empty()) {
            return -100;
        }

        for (int channel = 0; channel < outputChannels; ++channel) {
            const int sourceChannel = channel % bottomBlob.c;
            const int offsetY = (channel / bottomBlob.c) % 2;
            const int offsetX = (channel / bottomBlob.c) / 2;
            const float* source = bottomBlob.channel(sourceChannel).row(offsetY) + offsetX;
            float* destination = topBlob.channel(channel);
            for (int y = 0; y < outputHeight; ++y) {
                for (int x = 0; x < outputWidth; ++x) {
                    *destination++ = *source;
                    source += 2;
                }
                source += bottomBlob.w;
            }
        }
        return 0;
    }
};

DEFINE_LAYER_CREATOR(YoloV5Focus)

struct LoadedModel {
    ModelSpec spec;
    std::string fingerprint;
    ncnn::Net network;
    std::vector<std::string> labels;
    std::mutex mutex;
};

std::mutex gModelsMutex;
std::unordered_map<std::string, std::shared_ptr<LoadedModel>> gModels;

bool setError(std::string* output, const std::string& value) {
    if (output != nullptr) {
        *output = value;
    }
    return false;
}

std::string trimLine(std::string value) {
    if (value.size() >= 3U
            && static_cast<unsigned char>(value[0]) == 0xEFU
            && static_cast<unsigned char>(value[1]) == 0xBBU
            && static_cast<unsigned char>(value[2]) == 0xBFU) {
        value.erase(0, 3);
    }
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) {
        value.pop_back();
    }
    return value;
}

bool loadLabels(const std::string& path, std::vector<std::string>* labels, std::string* error) {
    if (labels == nullptr) {
        return setError(error, "YOLO 标签输出容器为空");
    }
    labels->clear();
    std::ifstream input(path);
    if (!input.is_open()) {
        return setError(error, "无法读取 YOLO 标签文件：" + path);
    }
    std::string line;
    while (std::getline(input, line)) {
        labels->push_back(trimLine(std::move(line)));
    }
    if (labels->empty()) {
        return setError(error, "YOLO 标签文件为空：" + path);
    }
    return true;
}

bool validateSpec(const ModelSpec& spec, std::string* error) {
    if (spec.name.empty()) {
        return setError(error, "YOLO 模型名称不能为空");
    }
    if (spec.labelsPath.empty() || spec.paramPath.empty() || spec.binPath.empty()) {
        return setError(error, "YOLO labels、param 和 bin 路径不能为空");
    }
    if (spec.inputBlob.empty()
            || spec.output8Blob.empty()
            || spec.output16Blob.empty()
            || spec.output32Blob.empty()) {
        return setError(error, "YOLO 输入或输出 blob 名称不能为空");
    }
    if (spec.useGpu) {
        return setError(error, "当前 libxiaoyv_yolo.so 仅包含 CPU NCNN 运行时，尚未启用 GPU/Vulkan");
    }
    return true;
}

std::string specFingerprint(const ModelSpec& spec) {
    constexpr char separator = '\x1F';
    return spec.labelsPath + separator
            + spec.paramPath + separator
            + spec.binPath + separator
            + spec.inputBlob + separator
            + spec.output8Blob + separator
            + spec.output16Blob + separator
            + spec.output32Blob + separator
            + (spec.useGpu ? "gpu" : "cpu");
}

float sigmoid(float value) {
    return 1.0F / (1.0F + std::exp(-value));
}

float intersectionArea(const Detection& first, const Detection& second) {
    const float left = std::max(first.x, second.x);
    const float top = std::max(first.y, second.y);
    const float right = std::min(first.x + first.width, second.x + second.width);
    const float bottom = std::min(first.y + first.height, second.y + second.height);
    return std::max(0.0F, right - left) * std::max(0.0F, bottom - top);
}

void applyNms(std::vector<Detection>* proposals, float threshold, std::vector<Detection>* detections) {
    if (proposals == nullptr || detections == nullptr) {
        return;
    }
    std::sort(proposals->begin(), proposals->end(), [](const Detection& first, const Detection& second) {
        return first.probability > second.probability;
    });

    detections->clear();
    for (const Detection& candidate : *proposals) {
        bool keep = true;
        for (const Detection& picked : *detections) {
            if (candidate.labelIndex != picked.labelIndex) {
                continue;
            }
            const float intersection = intersectionArea(candidate, picked);
            const float unionArea = candidate.width * candidate.height + picked.width * picked.height - intersection;
            if (unionArea > 0.0F && intersection / unionArea > threshold) {
                keep = false;
                break;
            }
        }
        if (keep) {
            detections->push_back(candidate);
        }
    }
}

bool generateProposals(
        const std::array<float, 6>& anchors,
        int stride,
        const ncnn::Mat& paddedInput,
        const ncnn::Mat& feature,
        float probabilityThreshold,
        std::vector<Detection>* proposals,
        std::string* error
) {
    if (proposals == nullptr) {
        return setError(error, "YOLO proposal 输出容器为空");
    }
    if (feature.dims != 3 || feature.w <= 5 || feature.h <= 0 || feature.c <= 0 || feature.elemsize != 4U) {
        return setError(error, "YOLO 输出 blob 形状不支持；仅支持标准 YOLOv5 三尺度 float 输出");
    }

    const int gridCells = feature.h;
    const int gridWidth = paddedInput.w / stride;
    const int gridHeight = paddedInput.h / stride;
    if (gridWidth <= 0 || gridHeight <= 0 || gridWidth * gridHeight != gridCells) {
        return setError(error, "YOLO 输出网格与输入尺寸不匹配；请确认模型输出 blob 配置");
    }
    const int classCount = feature.w - 5;
    const int anchorCount = std::min(feature.c, static_cast<int>(anchors.size() / 2U));
    for (int anchorIndex = 0; anchorIndex < anchorCount; ++anchorIndex) {
        const float anchorWidth = anchors[static_cast<size_t>(anchorIndex) * 2U];
        const float anchorHeight = anchors[static_cast<size_t>(anchorIndex) * 2U + 1U];
        const ncnn::Mat channel = feature.channel(anchorIndex);
        for (int gridY = 0; gridY < gridHeight; ++gridY) {
            for (int gridX = 0; gridX < gridWidth; ++gridX) {
                const float* values = channel.row(gridY * gridWidth + gridX);
                const float objectness = sigmoid(values[4]);
                if (objectness < probabilityThreshold) {
                    continue;
                }

                int labelIndex = 0;
                float bestClass = -std::numeric_limits<float>::infinity();
                for (int classIndex = 0; classIndex < classCount; ++classIndex) {
                    if (values[5 + classIndex] > bestClass) {
                        bestClass = values[5 + classIndex];
                        labelIndex = classIndex;
                    }
                }
                const float probability = objectness * sigmoid(bestClass);
                if (probability < probabilityThreshold) {
                    continue;
                }

                const float centerX = (sigmoid(values[0]) * 2.0F - 0.5F + static_cast<float>(gridX))
                        * static_cast<float>(stride);
                const float centerY = (sigmoid(values[1]) * 2.0F - 0.5F + static_cast<float>(gridY))
                        * static_cast<float>(stride);
                const float width = std::pow(sigmoid(values[2]) * 2.0F, 2.0F) * anchorWidth;
                const float height = std::pow(sigmoid(values[3]) * 2.0F, 2.0F) * anchorHeight;
                proposals->push_back({
                        centerX - width * 0.5F,
                        centerY - height * 0.5F,
                        width,
                        height,
                        labelIndex,
                        probability
                });
            }
        }
    }
    return true;
}

bool validateDetectOptions(const DetectOptions& options, std::string* error) {
    if (options.targetSize < 32 || options.targetSize > 4096) {
        return setError(error, "YOLO targetSize 必须在 32 到 4096 之间");
    }
    if (options.threads < 1 || options.threads > 32) {
        return setError(error, "YOLO threads 必须在 1 到 32 之间");
    }
    if (!std::isfinite(options.probabilityThreshold)
            || options.probabilityThreshold < 0.0F
            || options.probabilityThreshold > 1.0F) {
        return setError(error, "YOLO probThreshold 必须在 0 到 1 之间");
    }
    if (!std::isfinite(options.nmsThreshold) || options.nmsThreshold < 0.0F || options.nmsThreshold > 1.0F) {
        return setError(error, "YOLO nmsThreshold 必须在 0 到 1 之间");
    }
    return true;
}

bool resolveRegion(
        int width,
        int height,
        int* left,
        int* top,
        int* right,
        int* bottom,
        std::string* error
) {
    if (left == nullptr || top == nullptr || right == nullptr || bottom == nullptr) {
        return setError(error, "YOLO 区域输出参数为空");
    }
    if (*left == 0 && *top == 0 && *right == 0 && *bottom == 0) {
        *right = width;
        *bottom = height;
        return true;
    }
    if (*left < 0 || *top < 0 || *right <= *left || *bottom <= *top || *right > width || *bottom > height) {
        return setError(error, "YOLO 检测区域超出图像范围");
    }
    return true;
}

void appendJsonString(std::string* output, const std::string& value) {
    if (output == nullptr) {
        return;
    }
    output->push_back('"');
    constexpr char kHex[] = "0123456789abcdef";
    for (unsigned char character : value) {
        switch (character) {
            case '"': output->append("\\\""); break;
            case '\\': output->append("\\\\"); break;
            case '\b': output->append("\\b"); break;
            case '\f': output->append("\\f"); break;
            case '\n': output->append("\\n"); break;
            case '\r': output->append("\\r"); break;
            case '\t': output->append("\\t"); break;
            default:
                if (character < 0x20U) {
                    output->append("\\u00");
                    output->push_back(kHex[(character >> 4U) & 0x0FU]);
                    output->push_back(kHex[character & 0x0FU]);
                } else {
                    output->push_back(static_cast<char>(character));
                }
                break;
        }
    }
    output->push_back('"');
}

void appendNumber(std::string* output, float value) {
    if (output == nullptr) {
        return;
    }
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(7) << value;
    output->append(stream.str());
}

std::string detectionJson(const std::vector<Detection>& detections, const std::vector<std::string>& labels) {
    std::string result = "{\"items\":[";
    for (size_t index = 0; index < detections.size(); ++index) {
        if (index != 0U) {
            result.push_back(',');
        }
        const Detection& detection = detections[index];
        result.append("{\"x\":");
        appendNumber(&result, detection.x);
        result.append(",\"y\":");
        appendNumber(&result, detection.y);
        result.append(",\"w\":");
        appendNumber(&result, detection.width);
        result.append(",\"h\":");
        appendNumber(&result, detection.height);
        result.append(",\"label\":");
        if (detection.labelIndex >= 0 && static_cast<size_t>(detection.labelIndex) < labels.size()) {
            appendJsonString(&result, labels[static_cast<size_t>(detection.labelIndex)]);
        } else {
            appendJsonString(&result, std::to_string(detection.labelIndex));
        }
        result.append(",\"prob\":");
        appendNumber(&result, detection.probability);
        result.push_back('}');
    }
    result.append("]}");
    return result;
}

bool detectWithModel(
        LoadedModel& model,
        const unsigned char* rgba,
        int imageWidth,
        int imageHeight,
        int offsetX,
        int offsetY,
        const DetectOptions& options,
        std::string* resultJson,
        std::string* error
) {
    int resizedWidth = imageWidth;
    int resizedHeight = imageHeight;
    float scale = 1.0F;
    if (imageWidth > imageHeight) {
        scale = static_cast<float>(options.targetSize) / static_cast<float>(imageWidth);
        resizedWidth = options.targetSize;
        resizedHeight = std::max(1, static_cast<int>(static_cast<float>(imageHeight) * scale));
    } else {
        scale = static_cast<float>(options.targetSize) / static_cast<float>(imageHeight);
        resizedHeight = options.targetSize;
        resizedWidth = std::max(1, static_cast<int>(static_cast<float>(imageWidth) * scale));
    }

    ncnn::Mat input = ncnn::Mat::from_pixels_resize(
            rgba,
            ncnn::Mat::PIXEL_RGBA2RGB,
            imageWidth,
            imageHeight,
            resizedWidth,
            resizedHeight
    );
    if (input.empty()) {
        return setError(error, "YOLO 输入点阵转换失败");
    }

    const int horizontalPadding = (resizedWidth + kMaxStride - 1) / kMaxStride * kMaxStride - resizedWidth;
    const int verticalPadding = (resizedHeight + kMaxStride - 1) / kMaxStride * kMaxStride - resizedHeight;
    ncnn::Mat paddedInput;
    ncnn::copy_make_border(
            input,
            paddedInput,
            verticalPadding / 2,
            verticalPadding - verticalPadding / 2,
            horizontalPadding / 2,
            horizontalPadding - horizontalPadding / 2,
            ncnn::BORDER_CONSTANT,
            114.0F
    );
    const float normalization[] = {1.0F / 255.0F, 1.0F / 255.0F, 1.0F / 255.0F};
    paddedInput.substract_mean_normalize(nullptr, normalization);

    // 当前 NCNN 把线程数保存在 Net::opt，Extractor 创建时复制该选项。模型互斥锁由调用方
    // 持有，因此同一模型的两次检测不会互相改写线程配置。
    model.network.opt.num_threads = options.threads;
    ncnn::Extractor extractor = model.network.create_extractor();
    if (extractor.input(model.spec.inputBlob.c_str(), paddedInput) != 0) {
        return setError(error, "YOLO 输入 blob 不存在或模型不接受当前输入：" + model.spec.inputBlob);
    }

    constexpr std::array<std::array<float, 6>, kExpectedOutputCount> kAnchors = {{
            {{10.0F, 13.0F, 16.0F, 30.0F, 33.0F, 23.0F}},
            {{30.0F, 61.0F, 62.0F, 45.0F, 59.0F, 119.0F}},
            {{116.0F, 90.0F, 156.0F, 198.0F, 373.0F, 326.0F}}
    }};
    constexpr std::array<int, kExpectedOutputCount> kStrides = {{8, 16, 32}};
    const std::array<std::string, kExpectedOutputCount> outputs = {{
            model.spec.output8Blob,
            model.spec.output16Blob,
            model.spec.output32Blob
    }};

    std::vector<Detection> proposals;
    for (size_t index = 0; index < outputs.size(); ++index) {
        ncnn::Mat feature;
        if (extractor.extract(outputs[index].c_str(), feature) != 0) {
            return setError(error, "YOLO 输出 blob 不存在：" + outputs[index] + "；请在 load options 中设置 outputs");
        }
        if (!generateProposals(
                    kAnchors[index],
                    kStrides[index],
                    paddedInput,
                    feature,
                    options.probabilityThreshold,
                    &proposals,
                    error)) {
            return false;
        }
    }

    std::vector<Detection> detections;
    applyNms(&proposals, options.nmsThreshold, &detections);
    for (Detection& detection : detections) {
        const float left = (detection.x - static_cast<float>(horizontalPadding / 2)) / scale;
        const float top = (detection.y - static_cast<float>(verticalPadding / 2)) / scale;
        const float right = (detection.x + detection.width - static_cast<float>(horizontalPadding / 2)) / scale;
        const float bottom = (detection.y + detection.height - static_cast<float>(verticalPadding / 2)) / scale;
        const float clippedLeft = std::clamp(left, 0.0F, static_cast<float>(imageWidth));
        const float clippedTop = std::clamp(top, 0.0F, static_cast<float>(imageHeight));
        const float clippedRight = std::clamp(right, 0.0F, static_cast<float>(imageWidth));
        const float clippedBottom = std::clamp(bottom, 0.0F, static_cast<float>(imageHeight));
        detection.x = clippedLeft + static_cast<float>(offsetX);
        detection.y = clippedTop + static_cast<float>(offsetY);
        detection.width = std::max(0.0F, clippedRight - clippedLeft);
        detection.height = std::max(0.0F, clippedBottom - clippedTop);
    }

    if (resultJson != nullptr) {
        *resultJson = detectionJson(detections, model.labels);
    }
    return true;
}

} // namespace

bool loadModel(const ModelSpec& spec, std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    if (!validateSpec(spec, error)) {
        return false;
    }
    const std::string fingerprint = specFingerprint(spec);
    std::lock_guard<std::mutex> modelsLock(gModelsMutex);
    const auto existing = gModels.find(spec.name);
    if (existing != gModels.end()) {
        if (existing->second->fingerprint == fingerprint) {
            return true;
        }
        return setError(error, "YOLO 同名模型已按其他配置加载；请先 release：" + spec.name);
    }

    auto model = std::make_shared<LoadedModel>();
    model->spec = spec;
    model->fingerprint = fingerprint;
    model->network.opt.use_vulkan_compute = false;
    model->network.register_custom_layer("YoloV5Focus", YoloV5Focus_layer_creator);
    if (!loadLabels(spec.labelsPath, &model->labels, error)) {
        return false;
    }
    if (model->network.load_param(spec.paramPath.c_str()) != 0) {
        return setError(error, "加载 YOLO param 失败：" + spec.paramPath);
    }
    if (model->network.load_model(spec.binPath.c_str()) != 0) {
        return setError(error, "加载 YOLO bin 失败：" + spec.binPath);
    }
    gModels.emplace(spec.name, std::move(model));
    return true;
}

bool releaseModel(const std::string& name, bool* released, std::string* error) {
    if (released == nullptr) {
        return setError(error, "YOLO released 输出参数为空");
    }
    *released = false;
    if (name.empty()) {
        return setError(error, "YOLO 模型名称不能为空");
    }
    std::lock_guard<std::mutex> modelsLock(gModelsMutex);
    const auto existing = gModels.find(name);
    if (existing == gModels.end()) {
        if (error != nullptr) {
            error->clear();
        }
        return true;
    }
    gModels.erase(existing);
    *released = true;
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool isModelLoaded(const std::string& name, bool* loaded, std::string* error) {
    if (loaded == nullptr) {
        return setError(error, "YOLO loaded 输出参数为空");
    }
    if (name.empty()) {
        return setError(error, "YOLO 模型名称不能为空");
    }
    std::lock_guard<std::mutex> modelsLock(gModelsMutex);
    *loaded = gModels.find(name) != gModels.end();
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool detectRgba(
        const std::string& name,
        const unsigned char* pixels,
        size_t pixelBytes,
        int width,
        int height,
        int left,
        int top,
        int right,
        int bottom,
        const DetectOptions& options,
        std::string* resultJson,
        std::string* error
) {
    if (resultJson == nullptr) {
        return setError(error, "YOLO 检测结果输出参数为空");
    }
    resultJson->clear();
    if (name.empty() || pixels == nullptr || width <= 0 || height <= 0) {
        return setError(error, "YOLO 模型名称或 RGBA 输入无效");
    }
    const size_t nativeWidth = static_cast<size_t>(width);
    const size_t nativeHeight = static_cast<size_t>(height);
    if (nativeWidth > std::numeric_limits<size_t>::max() / nativeHeight) {
        return setError(error, "YOLO RGBA 输入尺寸过大");
    }
    const size_t pixelCount = nativeWidth * nativeHeight;
    if (pixelCount > std::numeric_limits<size_t>::max() / kChannels) {
        return setError(error, "YOLO RGBA 输入尺寸过大");
    }
    const size_t expectedBytes = pixelCount * kChannels;
    if (pixelBytes < expectedBytes) {
        return setError(error, "YOLO RGBA 缓冲区长度不足");
    }
    if (!validateDetectOptions(options, error)
            || !resolveRegion(width, height, &left, &top, &right, &bottom, error)) {
        return false;
    }

    std::shared_ptr<LoadedModel> model;
    {
        std::lock_guard<std::mutex> modelsLock(gModelsMutex);
        const auto existing = gModels.find(name);
        if (existing == gModels.end()) {
            return setError(error, "YOLO 模型未加载：" + name);
        }
        model = existing->second;
    }

    const int regionWidth = right - left;
    const int regionHeight = bottom - top;
    const unsigned char* regionPixels = pixels;
    std::vector<unsigned char> regionCopy;
    if (left != 0 || top != 0 || right != width || bottom != height) {
        regionCopy.resize(static_cast<size_t>(regionWidth) * static_cast<size_t>(regionHeight) * kChannels);
        for (int row = 0; row < regionHeight; ++row) {
            const unsigned char* source = pixels
                    + (static_cast<size_t>(top + row) * static_cast<size_t>(width) + static_cast<size_t>(left)) * kChannels;
            unsigned char* destination = regionCopy.data() + static_cast<size_t>(row) * static_cast<size_t>(regionWidth) * kChannels;
            std::copy_n(source, static_cast<size_t>(regionWidth) * kChannels, destination);
        }
        regionPixels = regionCopy.data();
    }

    std::lock_guard<std::mutex> modelLock(model->mutex);
    if (!detectWithModel(
                *model,
                regionPixels,
                regionWidth,
                regionHeight,
                left,
                top,
                options,
                resultJson,
                error)) {
        resultJson->clear();
        return false;
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

std::string runtimeInfoJson() {
#ifdef NCNN_VERSION_STRING
    constexpr const char* kNcnnVersion = NCNN_VERSION_STRING;
#else
    constexpr const char* kNcnnVersion = "unknown";
#endif
    return std::string("{\"backend\":\"ncnn\",\"ncnnVersion\":\"")
            + kNcnnVersion
            + "\",\"sourceTag\":\"20260526\",\"gpu\":false,\"model\":\"yolov5-ncnn\"}";
}

} // namespace xiaoyv::yolo
