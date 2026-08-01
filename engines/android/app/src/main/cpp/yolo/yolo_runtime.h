/**
 * 文件用途：声明可选 libxiaoyv_yolo.so 的 NCNN YOLOv5 运行时。
 *
 * 该层只处理模型、RGBA 点阵和推理结果；它不知道 Lua、JS、Go 或 Android Java 的对象类型。
 * 上层 Java/JNI 与 libengine.so 负责跨语言参数和 JSON 信封。
 */
#pragma once

#include <cstddef>
#include <string>

namespace xiaoyv::yolo {

/** 模型加载时固定的 NCNN YOLOv5 图结构信息。 */
struct ModelSpec {
    std::string name;
    std::string labelsPath;
    std::string paramPath;
    std::string binPath;
    std::string inputBlob = "images";
    std::string output8Blob = "output";
    std::string output16Blob = "353";
    std::string output32Blob = "367";
    bool useGpu = false;
};

/** 单次检测可调整的性能与筛选参数。 */
struct DetectOptions {
    int targetSize = 640;
    int threads = 2;
    float probabilityThreshold = 0.25F;
    float nmsThreshold = 0.45F;
};

/** 加载或复用一个按名称管理的模型；成功时 error 清空。 */
bool loadModel(const ModelSpec& spec, std::string* error);

/** 释放一个模型名称；不存在时 released 为 false，但不是平台错误。 */
bool releaseModel(const std::string& name, bool* released, std::string* error);

/** 查询模型名称是否仍处于加载状态。 */
bool isModelLoaded(const std::string& name, bool* loaded, std::string* error);

/**
 * 在紧凑 RGBA8888 点阵的指定区域执行检测。
 *
 * 区域使用左闭右开坐标；结果 JSON 为 {"items":[{"x","y","w","h","label","prob"}]}，
 * 坐标相对完整输入点阵而非裁剪区域。
 */
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
);

/** 返回不依赖模型文件的运行时能力描述 JSON。 */
std::string runtimeInfoJson();

} // namespace xiaoyv::yolo
