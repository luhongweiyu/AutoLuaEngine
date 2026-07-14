/**
 * 文件用途：声明 Android 设备能力的统一 core/api 入口，供稳定 C ABI 复用。
 */
#pragma once

#include <string>

#include "../../engine/json_value.h"

namespace xiaoyv::api {

/**
 * 调用 Android 平台设备能力。
 *
 * 设备信息、应用管理和系统控制都必须先经过这一层，再由 AndroidBridge 转到 Java。
 * Lua、JS、Go 和外部插件只调用 system_c_api，不得各自直接调用 Java，因此同一能力
 * 的参数校验、结果结构和错误文本能保持一致。
 *
 * operation 是 libengine.so 内部固定的能力名；arguments 必须是 JSON 对象。成功时
 * value 写入 Android 平台返回的 JSON 值，失败时 error 写入可直接返回给脚本的中文原因。
 */
bool callDeviceApi(
        const std::string& operation,
        const JsonValue& arguments,
        JsonValue* value,
        std::string* error
);

} // namespace xiaoyv::api
