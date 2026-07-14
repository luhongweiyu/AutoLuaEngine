/**
 * 文件用途：声明脚本 UI 会话核心 API，统一服务 Lua、JS、Go 和插件绑定。
 */
#pragma once

#include <string>

#include "runtime_api.h"

namespace xiaoyv::api {

/**
 * 创建一个脚本 UI 会话。
 *
 * surface 目前支持 dialog、hud、web。specJson 是由语言绑定生成的 JSON 配置，
 * Android 平台宿主负责把它渲染成对应原生界面。成功时写入 sessionId。
 */
bool openUiSurface(const std::string& surface, const std::string& specJson, long long* sessionId);

/**
 * 更新一个仍然存活的 UI 会话。
 *
 * 当前 HUD 使用此接口更新文字、位置、样式和按钮；Web 会话使用 postUiMessage
 * 发送页面消息。保留统一更新入口，供后续平台和语言绑定复用。
 */
bool updateUiSurface(long long sessionId, const std::string& specJson);

/**
 * 向 Web UI 页面发送 JSON 消息。
 */
bool postUiMessage(long long sessionId, const std::string& messageJson);

/**
 * 关闭一个 UI 会话并唤醒正在等待该会话事件的脚本。
 */
bool closeUiSurface(long long sessionId);

/**
 * 关闭当前引擎内所有脚本 UI 会话。
 *
 * 脚本结束、停止或强停前调用此接口，确保弹窗、HUD 和网页不会遗留在用户屏幕上。
 */
void closeAllUiSurfaces();

/**
 * 等待 UI 事件。
 *
 * timeoutMs 小于 0 表示无限等待，等于 0 表示仅立即检查一次。正常返回时 eventJson
 * 为 {"type":...,"data":...}；超时返回 type=timeout。shouldStop 非空时会在
 * 等待过程中持续检查，返回 false 表示脚本停止或会话不可用。
 */
bool waitUiEvent(
        long long sessionId,
        int timeoutMs,
        ShouldStopCallback shouldStop,
        void* stopContext,
        std::string* eventJson
);

/**
 * 接收 Android UI 宿主回传的事件。
 *
 * eventDataJson 必须是完整 JSON 值文本，通常是对象、数组、字符串、数字、布尔或 null。
 */
bool deliverUiEvent(
        long long sessionId,
        const std::string& eventType,
        const std::string& eventDataJson
);

/**
 * 返回当前线程最近一次 UI API 失败原因。
 */
std::string uiLastError();

} // namespace xiaoyv::api
