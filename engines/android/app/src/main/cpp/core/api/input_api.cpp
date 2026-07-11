/**
 * 文件用途：实现 Root 输入注入核心 API，统一校验参数并转发到 AndroidBridge。
 */
#include "input_api.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <unordered_map>

#include "../../platform/android_bridge.h"

namespace autolua::api {
namespace {

thread_local std::string gInputLastError;

bool 设置输入错误(const std::string& error) {
    gInputLastError = error;
    return false;
}

std::string 小写文本(const char* text) {
    std::string value = text == nullptr ? "" : text;
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool 校验手指参数(int id, int x, int y) {
    if (id < 0 || id > 4) {
        return 设置输入错误("touch id must be between 0 and 4");
    }
    if (x < 0 || y < 0) {
        return 设置输入错误("touch x/y must be greater than or equal to 0");
    }
    return true;
}

int 解析按键码(const char* keyCodeText) {
    static const std::unordered_map<std::string, int> kKeyCodes = {
            {"home", 3},
            {"back", 4},
            {"call", 5},
            {"endcall", 6},
            {"volup", 24},
            {"volumeup", 24},
            {"voldown", 25},
            {"volumedown", 25},
            {"power", 26},
            {"camera", 27},
            {"menu", 82},
            {"pageup", 92},
            {"pagedown", 93}
    };

    std::string value = 小写文本(keyCodeText);
    if (value.empty()) {
        设置输入错误("keycode is empty");
        return 0;
    }

    char* end = nullptr;
    long code = std::strtol(value.c_str(), &end, 10);
    if (end != value.c_str() && end != nullptr && *end == '\0' && code > 0 && code <= 4096) {
        return static_cast<int>(code);
    }

    auto iterator = kKeyCodes.find(value);
    if (iterator != kKeyCodes.end()) {
        return iterator->second;
    }

    设置输入错误("unknown keycode: " + value);
    return 0;
}

bool 执行输入命令(bool ok, const std::string& error) {
    if (!ok) {
        return 设置输入错误(error);
    }
    gInputLastError.clear();
    return true;
}

} // namespace

bool inputTouchDown(int id, int x, int y) {
    if (!校验手指参数(id, x, y)) {
        return false;
    }
    return 执行输入命令(AndroidBridge::touchDown(id, x, y), "root touchDown failed");
}

bool inputTouchMove(int id, int x, int y) {
    if (!校验手指参数(id, x, y)) {
        return false;
    }
    return 执行输入命令(AndroidBridge::touchMove(id, x, y), "root touchMove failed");
}

bool inputTouchUp(int id) {
    if (id < 0 || id > 4) {
        return 设置输入错误("touch id must be between 0 and 4");
    }
    return 执行输入命令(AndroidBridge::touchUp(id), "root touchUp failed");
}

bool inputKeyDown(const char* keyCodeText) {
    int keyCode = 解析按键码(keyCodeText);
    if (keyCode <= 0) {
        return false;
    }
    return 执行输入命令(AndroidBridge::keyDown(keyCode), "root keyDown failed");
}

bool inputKeyUp(const char* keyCodeText) {
    int keyCode = 解析按键码(keyCodeText);
    if (keyCode <= 0) {
        return false;
    }
    return 执行输入命令(AndroidBridge::keyUp(keyCode), "root keyUp failed");
}

bool inputKeyPress(const char* keyCodeText) {
    int keyCode = 解析按键码(keyCodeText);
    if (keyCode <= 0) {
        return false;
    }
    return 执行输入命令(AndroidBridge::keyPress(keyCode), "root keyPress failed");
}

bool inputText(const char* text) {
    return 执行输入命令(AndroidBridge::inputText(text == nullptr ? "" : text), "root inputText failed");
}

std::string getRunEnvType() {
    return AndroidBridge::isRootRuntimeReady() ? "root" : "none";
}

const std::string& inputLastError() {
    return gInputLastError;
}

} // namespace autolua::api
