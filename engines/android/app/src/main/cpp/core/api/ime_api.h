/**
 * 文件用途：声明应用输入法核心 API，供 Lua、JS、Go 和插件共用。
 */
#pragma once

#include <string>

namespace xiaoyv::api {

/**
 * 锁定 小鱼精灵 输入法。
 *
 * 该操作保存系统原默认输入法，并把 小鱼精灵 的无界面输入法设为默认输入法。
 * 切换系统输入法需要 Root helper；不会回退到按键注入或无障碍路线。
 */
bool imeLock();

/**
 * 通过已经锁定的 小鱼精灵 输入法提交完整 Unicode 文本。
 *
 * 调用前必须先 imeLock，并保证目标输入框已获得焦点。该操作只使用当前输入法服务，
 * 不会重复执行 Root 命令或切换输入法。
 */
bool imeSetText(const char* text);

/**
 * 解锁 小鱼精灵 输入法并恢复 lock 前保存的系统默认输入法。
 */
bool imeUnlock();

/**
 * 返回当前线程最近一次输入法 API 失败原因。
 */
const std::string& imeLastError();

} // namespace xiaoyv::api
