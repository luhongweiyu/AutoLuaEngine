/**
 * 文件用途：声明 Root 输入注入核心 API，供 Lua/JS/Go 和插件统一复用。
 */
#pragma once

#include <string>

namespace autolua::api {

/**
 * 按住一个模拟手指。
 *
 * id 只能取 0 到 4。该能力只走 Root helper 输入注入，不走无障碍。
 */
bool inputTouchDown(int id, int x, int y);

/**
 * 移动一个已按下的模拟手指。
 *
 * 返回 true 表示 root helper 已接受并成功注入事件。
 */
bool inputTouchMove(int id, int x, int y);

/**
 * 弹起一个模拟手指。
 */
bool inputTouchUp(int id);

/**
 * 按下一个按键不弹起。
 *
 * keyCodeText 支持数字字符串和常用标识符，例如 Home、Back、VolUp。
 */
bool inputKeyDown(const char* keyCodeText);

/**
 * 弹起一个按键。
 */
bool inputKeyUp(const char* keyCodeText);

/**
 * 按一下按键并弹起。
 */
bool inputKeyPress(const char* keyCodeText);

/**
 * 使用 Root 注入按键事件的方式输入文字。
 *
 * 适合英文、数字和常见符号；更完整的中文输入后续应通过独立 IME 能力实现。
 */
bool inputText(const char* text);

/**
 * 返回当前运行环境。
 *
 * 当前已完成的输入注入能力只返回 root 或 none。accessibility 不用于这些函数。
 */
std::string getRunEnvType();

/**
 * 返回最近一次输入 API 失败原因。
 */
const std::string& inputLastError();

} // namespace autolua::api
