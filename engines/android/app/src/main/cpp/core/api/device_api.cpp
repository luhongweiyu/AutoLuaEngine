/**
 * 文件用途：实现设备 core/api 到 Android 平台桥的统一调用和 JSON 结果校验。
 */
#include "device_api.h"

#include "../../platform/android_bridge.h"

namespace xiaoyv::api {

bool callDeviceApi(
        const std::string& operation,
        const JsonValue& arguments,
        JsonValue* value,
        std::string* error
) {
    if (value == nullptr) {
        if (error != nullptr) {
            *error = "设备 API 输出参数为空";
        }
        return false;
    }
    if (operation.empty()) {
        if (error != nullptr) {
            *error = "设备 API 名称不能为空";
        }
        return false;
    }
    if (!arguments.isObject()) {
        if (error != nullptr) {
            *error = "设备 API 参数必须是对象";
        }
        return false;
    }

    AndroidDeviceCallResult response = AndroidBridge::callDeviceApi(
            operation,
            jsonValueToString(arguments)
    );
    if (!response.invoked) {
        if (error != nullptr) {
            *error = response.error.empty() ? "调用 Android 设备能力失败" : response.error;
        }
        return false;
    }

    JsonValue envelope;
    std::string parseError;
    if (!parseJsonText(response.responseJson, &envelope, &parseError) || !envelope.isObject()) {
        if (error != nullptr) {
            *error = "Android 设备能力返回 JSON 无效";
        }
        return false;
    }

    if (!envelope.boolOr("ok", false)) {
        if (error != nullptr) {
            std::string message = envelope.stringOr("error");
            *error = message.empty() ? "Android 设备能力执行失败" : message;
        }
        return false;
    }

    const JsonValue* responseValue = envelope.get("value");
    *value = responseValue == nullptr ? JsonValue::makeNull() : *responseValue;
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

} // namespace xiaoyv::api
