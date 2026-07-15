/**
 * 文件用途：实现 RapidOCR C++ 核心门面，统一模型参数、JSON 契约和 Android ONNX 平台桥调用。
 */
#include "ocr_api.h"

#include <map>
#include <string>
#include <utility>

#include "../../engine/json_value.h"
#include "../../platform/android_bridge.h"

namespace xiaoyv::api {
namespace {

thread_local std::string gOcrLastError;

/** 记录当前线程失败原因并统一返回 false。 */
bool 设置OCR错误(const std::string& error) {
    gOcrLastError = error;
    return false;
}

/** 确保 C ABI 传入的文本不为空且不包含错误的 null 指针。 */
std::string 安全文本(const char* value) {
    return value == nullptr ? "" : value;
}

/** 将可选 options JSON 解析为对象；不允许数组或标量进入 OCR 平台桥。 */
bool 解析选项(const char* optionsJson, JsonValue* options) {
    if (options == nullptr) {
        return 设置OCR错误("OCR 选项输出对象为空");
    }
    std::string source = 安全文本(optionsJson);
    if (source.empty()) {
        *options = JsonValue::makeObject({});
        return true;
    }
    std::string error;
    if (!parseJsonText(source, options, &error) || !options->isObject()) {
        return 设置OCR错误("OCR options 必须是 JSON 对象：" + error);
    }
    return true;
}

/** 构造 OCR 平台固定参数对象。 */
JsonValue OCR参数(std::initializer_list<std::pair<std::string, JsonValue>> values) {
    std::map<std::string, JsonValue> object;
    for (const auto& value : values) {
        object.emplace(value.first, value.second);
    }
    return JsonValue::makeObject(std::move(object));
}

/**
 * 调用 Java OCR 平台，并解析统一 JSON 信封。
 *
 * Java 侧只负责 ONNX Runtime 与 Bitmap，固定接口的参数校验、返回 JSON 所有权和错误语义
 * 都在本层统一，后续 JS/Go 绑定无需再了解 Java 结果格式。
 */
bool 调用OCR平台(const char* operation, const JsonValue& arguments, JsonValue* data) {
    if (data == nullptr) {
        return 设置OCR错误("OCR 返回数据对象为空");
    }
    AndroidOcrCallResult bridgeResult = AndroidBridge::callOcrApi(
            operation == nullptr ? "" : operation,
            jsonValueToString(arguments)
    );
    if (!bridgeResult.invoked) {
        return 设置OCR错误(bridgeResult.error.empty() ? "Android OCR 平台调用失败" : bridgeResult.error);
    }

    JsonValue envelope;
    std::string parseError;
    if (!parseJsonText(bridgeResult.responseJson, &envelope, &parseError) || !envelope.isObject()) {
        return 设置OCR错误("Android OCR 返回 JSON 无效：" + parseError);
    }
    const JsonValue* ok = envelope.get("ok");
    if (ok == nullptr || !ok->isBool()) {
        return 设置OCR错误("Android OCR 返回结果缺少 ok 字段");
    }
    if (!ok->boolValue()) {
        const JsonValue* error = envelope.get("error");
        return 设置OCR错误(error != nullptr && error->isString()
                ? error->stringValue()
                : "Android OCR 调用失败");
    }
    const JsonValue* returnedData = envelope.get("data");
    if (returnedData == nullptr) {
        return 设置OCR错误("Android OCR 返回结果缺少 data 字段");
    }
    *data = *returnedData;
    gOcrLastError.clear();
    return true;
}

/** 校验用户模型名称不为空，错误在 core 层返回而非交给 Java 隐式处理。 */
bool 校验模型名称(const char* name, std::string* output) {
    if (output == nullptr) {
        return 设置OCR错误("OCR 模型名称输出对象为空");
    }
    *output = 安全文本(name);
    if (output->empty()) {
        return 设置OCR错误("OCR 模型名称不能为空");
    }
    return true;
}

} // namespace

bool 加载OCR模型(
        const char* name,
        const char* detPath,
        const char* recPath,
        const char* clsPath,
        const char* keysPath,
        int threads
) {
    std::string modelName;
    if (!校验模型名称(name, &modelName)) {
        return false;
    }
    if (threads <= 0 || threads > 32) {
        return 设置OCR错误("OCR 线程数必须在 1 到 32 之间");
    }
    if (安全文本(detPath).empty() || 安全文本(recPath).empty() || 安全文本(keysPath).empty()) {
        return 设置OCR错误("OCR det、rec 和 keys 模型路径不能为空");
    }

    JsonValue ignored;
    return 调用OCR平台("load", OCR参数({
            {"name", JsonValue::makeString(modelName)},
            {"det", JsonValue::makeString(安全文本(detPath))},
            {"rec", JsonValue::makeString(安全文本(recPath))},
            {"cls", JsonValue::makeString(安全文本(clsPath))},
            {"keys", JsonValue::makeString(安全文本(keysPath))},
            {"threads", JsonValue::makeNumber(threads)}
    }), &ignored);
}

bool 释放OCR模型(const char* name, bool* released) {
    if (released == nullptr) {
        return 设置OCR错误("OCR 释放结果输出对象为空");
    }
    *released = false;
    std::string modelName;
    if (!校验模型名称(name, &modelName)) {
        return false;
    }

    JsonValue data;
    if (!调用OCR平台("release", OCR参数({
            {"name", JsonValue::makeString(modelName)}
    }), &data)) {
        return false;
    }
    const JsonValue* value = data.get("released");
    if (value == nullptr || !value->isBool()) {
        return 设置OCR错误("OCR 释放返回值无效");
    }
    *released = value->boolValue();
    return true;
}

bool OCR模型已加载(const char* name, bool* loaded) {
    if (loaded == nullptr) {
        return 设置OCR错误("OCR 加载状态输出对象为空");
    }
    *loaded = false;
    std::string modelName;
    if (!校验模型名称(name, &modelName)) {
        return false;
    }

    JsonValue data;
    if (!调用OCR平台("isLoaded", OCR参数({
            {"name", JsonValue::makeString(modelName)}
    }), &data)) {
        return false;
    }
    const JsonValue* value = data.get("loaded");
    if (value == nullptr || !value->isBool()) {
        return 设置OCR错误("OCR 加载状态返回值无效");
    }
    *loaded = value->boolValue();
    return true;
}

bool 识别图片文字(
        const char* name,
        const char* imagePath,
        const char* optionsJson,
        std::string* resultJson
) {
    if (resultJson == nullptr) {
        return 设置OCR错误("OCR 识别结果输出对象为空");
    }
    resultJson->clear();
    std::string modelName;
    if (!校验模型名称(name, &modelName)) {
        return false;
    }
    std::string path = 安全文本(imagePath);
    if (path.empty()) {
        return 设置OCR错误("OCR 图片路径不能为空");
    }
    JsonValue options;
    if (!解析选项(optionsJson, &options)) {
        return false;
    }

    JsonValue data;
    if (!调用OCR平台("read", OCR参数({
            {"name", JsonValue::makeString(modelName)},
            {"path", JsonValue::makeString(path)},
            {"options", options}
    }), &data)) {
        return false;
    }
    *resultJson = jsonValueToString(data);
    return true;
}

bool 在图片中找文字(
        const char* name,
        const char* imagePath,
        const char* text,
        const char* optionsJson,
        std::string* resultJson
) {
    if (resultJson == nullptr) {
        return 设置OCR错误("OCR 找字结果输出对象为空");
    }
    resultJson->clear();
    std::string modelName;
    if (!校验模型名称(name, &modelName)) {
        return false;
    }
    std::string path = 安全文本(imagePath);
    std::string targetText = 安全文本(text);
    if (path.empty() || targetText.empty()) {
        return 设置OCR错误("OCR 图片路径和要查找的文字不能为空");
    }
    JsonValue options;
    if (!解析选项(optionsJson, &options)) {
        return false;
    }

    JsonValue data;
    if (!调用OCR平台("findText", OCR参数({
            {"name", JsonValue::makeString(modelName)},
            {"path", JsonValue::makeString(path)},
            {"text", JsonValue::makeString(targetText)},
            {"options", options}
    }), &data)) {
        return false;
    }
    *resultJson = jsonValueToString(data);
    return true;
}

std::string 取OCR错误() {
    return gOcrLastError;
}

} // namespace xiaoyv::api
