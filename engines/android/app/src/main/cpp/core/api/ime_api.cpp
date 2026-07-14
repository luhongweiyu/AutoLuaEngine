/**
 * 文件用途：实现应用输入法核心 API，并统一转发到 Android 平台桥。
 */
#include "ime_api.h"

#include "../../platform/android_bridge.h"

namespace xiaoyv::api {
namespace {

thread_local std::string gImeLastError;

/**
 * 统一记录输入法命令失败原因，避免 Lua、JS、Go 和插件各自维护错误状态。
 */
bool 执行输入法命令(bool succeeded, const char* error) {
    if (!succeeded) {
        gImeLastError = error;
        return false;
    }

    gImeLastError.clear();
    return true;
}

} // namespace

bool imeLock() {
    return 执行输入法命令(AndroidBridge::imeLock(), "锁定小鱼精灵输入法失败");
}

bool imeSetText(const char* text) {
    return 执行输入法命令(
            AndroidBridge::imeSetText(text == nullptr ? "" : text),
            "通过小鱼精灵输入法设置文字失败"
    );
}

bool imeUnlock() {
    return 执行输入法命令(AndroidBridge::imeUnlock(), "恢复系统输入法失败");
}

const std::string& imeLastError() {
    return gImeLastError;
}

} // namespace xiaoyv::api
