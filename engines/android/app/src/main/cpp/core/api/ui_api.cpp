/**
 * 文件用途：实现脚本 UI 会话和事件队列，隔离语言运行时与 Android 界面线程。
 */
#include "ui_api.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../../engine/json_value.h"
#include "../../platform/android_bridge.h"

namespace xiaoyv::api {
namespace {

/**
 * 单个 UI 会话的事件状态。
 *
 * 会话对象通过 shared_ptr 保存，close 后从全局表移除，但已经在 waitUiEvent 中取到的
 * 等待线程仍能安全收到 closed 事件，不会访问悬空对象。
 */
struct UiSession {
    explicit UiSession(std::string surfaceName) : surface(std::move(surfaceName)) {
    }

    std::string surface;
    bool closed = false;
    std::deque<std::pair<std::string, std::string>> events;
    std::condition_variable condition;
};

std::mutex gUiMutex;
std::unordered_map<long long, std::shared_ptr<UiSession>> gUiSessions;
long long gNextUiSessionId = 1;
thread_local std::string gUiLastError;

bool setError(const std::string& error) {
    gUiLastError = error;
    return false;
}

bool isSupportedSurface(const std::string& surface) {
    return surface == "dialog" || surface == "hud" || surface == "web";
}

bool showSurfaceOnAndroid(
        const std::string& surface,
        long long sessionId,
        const std::string& specJson
) {
    if (surface == "dialog") {
        return AndroidBridge::showScriptDialog(sessionId, specJson);
    }
    if (surface == "hud") {
        return AndroidBridge::showScriptHud(sessionId, specJson);
    }
    if (surface == "web") {
        return AndroidBridge::showScriptWeb(sessionId, specJson);
    }
    return false;
}

bool updateSurfaceOnAndroid(
        const std::string& surface,
        long long sessionId,
        const std::string& specJson
) {
    if (surface == "hud") {
        return AndroidBridge::updateScriptHud(sessionId, specJson);
    }
    return setError("当前界面类型不支持更新：" + surface);
}

std::string makeEventJson(const std::string& eventType, const std::string& eventDataJson) {
    return "{\"type\":"
            + quoteJsonString(eventType)
            + ",\"data\":"
            + (eventDataJson.empty() ? "null" : eventDataJson)
            + "}";
}

std::shared_ptr<UiSession> findSessionLocked(long long sessionId) {
    auto iterator = gUiSessions.find(sessionId);
    return iterator == gUiSessions.end() ? nullptr : iterator->second;
}

} // namespace

bool openUiSurface(const std::string& surface, const std::string& specJson, long long* sessionId) {
    if (sessionId == nullptr) {
        return setError("UI 会话输出地址为空");
    }
    if (!isSupportedSurface(surface)) {
        return setError("不支持的界面类型：" + surface);
    }

    long long createdSessionId;
    {
        std::lock_guard<std::mutex> lock(gUiMutex);
        createdSessionId = gNextUiSessionId++;
        if (createdSessionId <= 0) {
            gNextUiSessionId = 1;
            createdSessionId = gNextUiSessionId++;
        }
        gUiSessions.emplace(createdSessionId, std::make_shared<UiSession>(surface));
    }

    if (!showSurfaceOnAndroid(surface, createdSessionId, specJson)) {
        std::lock_guard<std::mutex> lock(gUiMutex);
        gUiSessions.erase(createdSessionId);
        return setError("Android UI 宿主未能打开界面");
    }

    *sessionId = createdSessionId;
    gUiLastError.clear();
    return true;
}

bool updateUiSurface(long long sessionId, const std::string& specJson) {
    std::string surface;
    {
        std::lock_guard<std::mutex> lock(gUiMutex);
        std::shared_ptr<UiSession> session = findSessionLocked(sessionId);
        if (session == nullptr || session->closed) {
            return setError("UI 会话不存在或已关闭");
        }
        surface = session->surface;
    }

    if (!updateSurfaceOnAndroid(surface, sessionId, specJson)) {
        if (gUiLastError.empty()) {
            setError("Android UI 宿主未能更新界面");
        }
        return false;
    }

    gUiLastError.clear();
    return true;
}

bool postUiMessage(long long sessionId, const std::string& messageJson) {
    std::string surface;
    {
        std::lock_guard<std::mutex> lock(gUiMutex);
        std::shared_ptr<UiSession> session = findSessionLocked(sessionId);
        if (session == nullptr || session->closed) {
            return setError("UI 会话不存在或已关闭");
        }
        surface = session->surface;
    }

    if (surface != "web") {
        return setError("只有 HTML 页面支持发送消息");
    }
    if (!AndroidBridge::postScriptWebMessage(sessionId, messageJson)) {
        return setError("Android HTML 页面未能接收消息");
    }

    gUiLastError.clear();
    return true;
}

bool closeUiSurface(long long sessionId) {
    std::shared_ptr<UiSession> session;
    {
        std::lock_guard<std::mutex> lock(gUiMutex);
        session = findSessionLocked(sessionId);
        if (session == nullptr) {
            return setError("UI 会话不存在");
        }
        session->closed = true;
        gUiSessions.erase(sessionId);
    }

    session->condition.notify_all();
    AndroidBridge::closeScriptUi(sessionId);
    gUiLastError.clear();
    return true;
}

void closeAllUiSurfaces() {
    std::vector<long long> sessionIds;
    std::vector<std::shared_ptr<UiSession>> sessions;
    {
        std::lock_guard<std::mutex> lock(gUiMutex);
        sessionIds.reserve(gUiSessions.size());
        sessions.reserve(gUiSessions.size());
        for (const auto& item : gUiSessions) {
            item.second->closed = true;
            sessionIds.push_back(item.first);
            sessions.push_back(item.second);
        }
        gUiSessions.clear();
    }

    for (const std::shared_ptr<UiSession>& session : sessions) {
        session->condition.notify_all();
    }
    for (long long sessionId : sessionIds) {
        AndroidBridge::closeScriptUi(sessionId);
    }
    gUiLastError.clear();
}

bool waitUiEvent(
        long long sessionId,
        int timeoutMs,
        ShouldStopCallback shouldStop,
        void* stopContext,
        std::string* eventJson
) {
    if (eventJson == nullptr) {
        return setError("UI 事件输出地址为空");
    }
    if (timeoutMs < -1) {
        return setError("UI 等待时间必须大于等于 -1");
    }

    std::shared_ptr<UiSession> session;
    {
        std::lock_guard<std::mutex> lock(gUiMutex);
        session = findSessionLocked(sessionId);
    }
    if (session == nullptr) {
        return setError("UI 会话不存在");
    }

    const auto deadline = timeoutMs < 0
            ? std::chrono::steady_clock::time_point::max()
            : std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    std::unique_lock<std::mutex> lock(gUiMutex);
    while (true) {
        if (!session->events.empty()) {
            std::pair<std::string, std::string> event = std::move(session->events.front());
            session->events.pop_front();
            *eventJson = makeEventJson(event.first, event.second);
            gUiLastError.clear();
            return true;
        }
        if (session->closed) {
            *eventJson = makeEventJson("closed", "null");
            gUiLastError.clear();
            return true;
        }

        lock.unlock();
        bool shouldStopNow = shouldStop != nullptr && shouldStop(stopContext);
        lock.lock();
        if (shouldStopNow) {
            return setError("脚本已停止");
        }

        if (timeoutMs >= 0 && std::chrono::steady_clock::now() >= deadline) {
            *eventJson = makeEventJson("timeout", "null");
            gUiLastError.clear();
            return true;
        }

        std::chrono::milliseconds slice(20);
        if (timeoutMs >= 0) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now()
            );
            if (remaining.count() <= 0) {
                continue;
            }
            if (remaining < slice) {
                slice = remaining;
            }
        }
        session->condition.wait_for(lock, slice);
    }
}

bool deliverUiEvent(
        long long sessionId,
        const std::string& eventType,
        const std::string& eventDataJson
) {
    if (eventType.empty()) {
        return setError("UI 事件类型为空");
    }

    std::shared_ptr<UiSession> session;
    {
        std::lock_guard<std::mutex> lock(gUiMutex);
        session = findSessionLocked(sessionId);
        if (session == nullptr || session->closed) {
            return setError("UI 会话不存在或已关闭");
        }
        session->events.emplace_back(eventType, eventDataJson.empty() ? "null" : eventDataJson);
    }

    session->condition.notify_all();
    gUiLastError.clear();
    return true;
}

std::string uiLastError() {
    return gUiLastError;
}

} // namespace xiaoyv::api
