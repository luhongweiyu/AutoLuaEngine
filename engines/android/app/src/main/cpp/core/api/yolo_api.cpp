/**
 * 文件用途：实现可选 YOLO 的 core/api 门面；模型推理由独立 SO 处理，core 只统一 C ABI、
 * 截图拷贝、图片解码和 JSON 契约。
 */
#include "yolo_api.h"

#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "../../engine/json_value.h"
#include "../../platform/android_bridge.h"
#include "screen_api.h"

namespace xiaoyv::api {
namespace {

constexpr size_t kRgbaBytesPerPixel = 4U;
thread_local std::string gYoloLastError;

bool 设置YOLO错误(const std::string& error) {
    gYoloLastError = error;
    return false;
}

std::string 安全文本(const char* value) {
    return value == nullptr ? "" : value;
}

bool 解析选项(const char* optionsJson, JsonValue* options) {
    if (options == nullptr) {
        return 设置YOLO错误("YOLO 选项输出对象为空");
    }
    const std::string source = 安全文本(optionsJson);
    if (source.empty()) {
        *options = JsonValue::makeObject({});
        return true;
    }
    std::string error;
    if (!parseJsonText(source, options, &error) || !options->isObject()) {
        return 设置YOLO错误("YOLO options 必须是 JSON 对象：" + error);
    }
    return true;
}

JsonValue YOLO参数(std::initializer_list<std::pair<std::string, JsonValue>> values = {}) {
    std::map<std::string, JsonValue> object;
    for (const auto& value : values) {
        object.emplace(value.first, value.second);
    }
    return JsonValue::makeObject(std::move(object));
}

bool 解析YOLO信封(const AndroidYoloCallResult& bridgeResult, JsonValue* data) {
    if (data == nullptr) {
        return 设置YOLO错误("YOLO 返回数据对象为空");
    }
    if (!bridgeResult.invoked) {
        return 设置YOLO错误(bridgeResult.error.empty() ? "Android YOLO 平台调用失败" : bridgeResult.error);
    }

    JsonValue envelope;
    std::string parseError;
    if (!parseJsonText(bridgeResult.responseJson, &envelope, &parseError) || !envelope.isObject()) {
        return 设置YOLO错误("Android YOLO 返回 JSON 无效：" + parseError);
    }
    const JsonValue* ok = envelope.get("ok");
    if (ok == nullptr || !ok->isBool()) {
        return 设置YOLO错误("Android YOLO 返回结果缺少 ok 字段");
    }
    if (!ok->boolValue()) {
        const JsonValue* error = envelope.get("error");
        return 设置YOLO错误(error != nullptr && error->isString()
                ? error->stringValue()
                : "Android YOLO 调用失败");
    }
    const JsonValue* returnedData = envelope.get("data");
    if (returnedData == nullptr) {
        return 设置YOLO错误("Android YOLO 返回结果缺少 data 字段");
    }
    *data = *returnedData;
    gYoloLastError.clear();
    return true;
}

bool 调用YOLO平台(const char* operation, const JsonValue& arguments, JsonValue* data) {
    return 解析YOLO信封(AndroidBridge::callYoloApi(
            operation == nullptr ? "" : operation,
            jsonValueToString(arguments)
    ), data);
}

bool 调用YOLORgba平台(
        const JsonValue& arguments,
        const unsigned char* pixels,
        size_t pixelBytes,
        int width,
        int height,
        JsonValue* data
) {
    return 解析YOLO信封(AndroidBridge::callYoloRgba(
            "detect",
            jsonValueToString(arguments),
            pixels,
            pixelBytes,
            width,
            height
    ), data);
}

bool 校验模型名称(const char* name, std::string* output) {
    if (output == nullptr) {
        return 设置YOLO错误("YOLO 模型名称输出对象为空");
    }
    *output = 安全文本(name);
    if (output->empty()) {
        return 设置YOLO错误("YOLO 模型名称不能为空");
    }
    return true;
}

bool 计算RGBA长度(int width, int height, size_t* byteCount) {
    if (byteCount == nullptr || width <= 0 || height <= 0) {
        return 设置YOLO错误("YOLO RGBA 图像尺寸无效");
    }
    const size_t nativeWidth = static_cast<size_t>(width);
    const size_t nativeHeight = static_cast<size_t>(height);
    if (nativeWidth > std::numeric_limits<size_t>::max() / nativeHeight
            || nativeWidth * nativeHeight > std::numeric_limits<size_t>::max() / kRgbaBytesPerPixel) {
        return 设置YOLO错误("YOLO RGBA 图像过大");
    }
    *byteCount = nativeWidth * nativeHeight * kRgbaBytesPerPixel;
    return true;
}

bool 检测RGBA(
        const std::string& modelName,
        const unsigned char* pixels,
        size_t pixelBytes,
        int width,
        int height,
        int left,
        int top,
        int right,
        int bottom,
        const JsonValue& options,
        std::string* resultJson
) {
    if (resultJson == nullptr) {
        return 设置YOLO错误("YOLO 检测结果输出对象为空");
    }
    resultJson->clear();
    size_t expectedBytes = 0;
    if (pixels == nullptr) {
        return 设置YOLO错误("YOLO RGBA 输入为空");
    }
    if (!计算RGBA长度(width, height, &expectedBytes)) {
        return false;
    }
    if (pixelBytes < expectedBytes) {
        return 设置YOLO错误("YOLO RGBA 缓冲区长度不足");
    }

    JsonValue data;
    if (!调用YOLORgba平台(YOLO参数({
            {"name", JsonValue::makeString(modelName)},
            {"left", JsonValue::makeNumber(left)},
            {"top", JsonValue::makeNumber(top)},
            {"right", JsonValue::makeNumber(right)},
            {"bottom", JsonValue::makeNumber(bottom)},
            {"options", options}
    }), pixels, expectedBytes, width, height, &data)) {
        return false;
    }
    if (!data.isObject()) {
        return 设置YOLO错误("YOLO 检测返回数据不是对象");
    }
    *resultJson = jsonValueToString(data);
    return true;
}

} // namespace

bool 获取YOLO运行时信息(std::string* resultJson) {
    if (resultJson == nullptr) {
        return 设置YOLO错误("YOLO 运行时信息输出对象为空");
    }
    resultJson->clear();
    JsonValue data;
    if (!调用YOLO平台("runtimeInfo", YOLO参数(), &data)) {
        return false;
    }
    if (!data.isObject()) {
        return 设置YOLO错误("YOLO 运行时信息不是对象");
    }
    const JsonValue* available = data.get("available");
    if (available == nullptr || !available->isBool()) {
        return 设置YOLO错误("YOLO 运行时信息缺少 available 布尔字段");
    }
    *resultJson = jsonValueToString(data);
    return true;
}

bool YOLO运行时可用(bool* available) {
    if (available == nullptr) {
        return 设置YOLO错误("YOLO 可用状态输出对象为空");
    }
    *available = false;
    std::string runtimeInfo;
    if (!获取YOLO运行时信息(&runtimeInfo)) {
        return false;
    }
    JsonValue data;
    std::string parseError;
    if (!parseJsonText(runtimeInfo, &data, &parseError)) {
        return 设置YOLO错误("YOLO 运行时信息 JSON 无效：" + parseError);
    }
    const JsonValue* value = data.get("available");
    if (value == nullptr || !value->isBool()) {
        return 设置YOLO错误("YOLO 运行时信息缺少 available 布尔字段");
    }
    *available = value->boolValue();
    return true;
}

bool 加载YOLO模型(
        const char* name,
        const char* labelsPath,
        const char* paramPath,
        const char* binPath,
        const char* optionsJson
) {
    std::string modelName;
    if (!校验模型名称(name, &modelName)) {
        return false;
    }
    const std::string labels = 安全文本(labelsPath);
    const std::string param = 安全文本(paramPath);
    const std::string bin = 安全文本(binPath);
    if (labels.empty() || param.empty() || bin.empty()) {
        return 设置YOLO错误("YOLO labels、param 和 bin 路径不能为空");
    }
    JsonValue options;
    if (!解析选项(optionsJson, &options)) {
        return false;
    }

    JsonValue ignored;
    return 调用YOLO平台("load", YOLO参数({
            {"name", JsonValue::makeString(modelName)},
            {"labels", JsonValue::makeString(labels)},
            {"param", JsonValue::makeString(param)},
            {"bin", JsonValue::makeString(bin)},
            {"options", options}
    }), &ignored);
}

bool 释放YOLO模型(const char* name, bool* released) {
    if (released == nullptr) {
        return 设置YOLO错误("YOLO released 输出对象为空");
    }
    *released = false;
    std::string modelName;
    if (!校验模型名称(name, &modelName)) {
        return false;
    }
    JsonValue data;
    if (!调用YOLO平台("release", YOLO参数({
            {"name", JsonValue::makeString(modelName)}
    }), &data)) {
        return false;
    }
    const JsonValue* value = data.get("released");
    if (value == nullptr || !value->isBool()) {
        return 设置YOLO错误("YOLO release 返回值无效");
    }
    *released = value->boolValue();
    return true;
}

bool YOLO模型已加载(const char* name, bool* loaded) {
    if (loaded == nullptr) {
        return 设置YOLO错误("YOLO loaded 输出对象为空");
    }
    *loaded = false;
    std::string modelName;
    if (!校验模型名称(name, &modelName)) {
        return false;
    }
    JsonValue data;
    if (!调用YOLO平台("isLoaded", YOLO参数({
            {"name", JsonValue::makeString(modelName)}
    }), &data)) {
        return false;
    }
    const JsonValue* value = data.get("loaded");
    if (value == nullptr || !value->isBool()) {
        return 设置YOLO错误("YOLO isLoaded 返回值无效");
    }
    *loaded = value->boolValue();
    return true;
}

bool 检测当前屏幕YOLO(
        const char* name,
        int left,
        int top,
        int right,
        int bottom,
        const char* optionsJson,
        std::string* resultJson
) {
    std::string modelName;
    if (!校验模型名称(name, &modelName)) {
        return false;
    }
    JsonValue options;
    if (!解析选项(optionsJson, &options)) {
        return false;
    }

    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    if (!copyScreenPixels(&pixels, &width, &height)) {
        return 设置YOLO错误("YOLO 获取当前截图失败：" + screenLastError());
    }
    return 检测RGBA(
            modelName,
            pixels.data(),
            pixels.size(),
            width,
            height,
            left,
            top,
            right,
            bottom,
            options,
            resultJson
    );
}

bool 检测图片YOLO(
        const char* name,
        const char* imagePath,
        const char* optionsJson,
        std::string* resultJson
) {
    std::string modelName;
    if (!校验模型名称(name, &modelName)) {
        return false;
    }
    const std::string path = 安全文本(imagePath);
    if (path.empty()) {
        return 设置YOLO错误("YOLO 图片路径不能为空");
    }
    JsonValue options;
    if (!解析选项(optionsJson, &options)) {
        return false;
    }

    AndroidImageDecodeResult image = AndroidBridge::decodeImageFile(path);
    if (!image.success || image.pixels.empty()) {
        return 设置YOLO错误("YOLO 图片解码失败：" + image.error);
    }
    return 检测RGBA(
            modelName,
            image.pixels.data(),
            image.pixels.size(),
            image.width,
            image.height,
            0,
            0,
            0,
            0,
            options,
            resultJson
    );
}

std::string 取YOLO错误() {
    return gYoloLastError;
}

} // namespace xiaoyv::api
