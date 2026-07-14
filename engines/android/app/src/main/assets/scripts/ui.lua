-- 文件用途：验证原生弹窗、HUD 和 HTML 页面三种脚本 UI。
local hud, hudError = m.hud.show("ui_status", {
    text = "脚本 UI 示例运行中",
    x = 24,
    y = 180,
    clickable = true,
    clickId = "status",
    buttons = {
        { id = "stop", text = "关闭" },
    },
})

if not hud then
    error("HUD 创建失败：" .. tostring(hudError))
end

local accepted, dialogError = m.dialog.confirm(
    "脚本 UI",
    "打开 HTML 示例页面？",
    "打开",
    "取消"
)
if accepted == nil then
    error("弹窗失败：" .. tostring(dialogError))
end

if not accepted then
    m.hud.hide("ui_status")
    return
end

local page, pageError = m.web.open({
    file = "ui_demo.html",
    title = "小鱼精灵 UI 示例",
})
if not page then
    m.hud.hide("ui_status")
    error("HTML 页面创建失败：" .. tostring(pageError))
end

while true do
    local event, waitError = m.web.waitEvent(page)
    if not event then
        print("页面事件失败：", waitError)
        break
    end

    print("页面事件：", event.type, event.data)
    if event.type == "submit" then
        m.web.postMessage(page, {
            message = "Lua 已收到：" .. tostring(event.data and event.data.name or ""),
        })
    elseif event.type == "closed" then
        break
    end
end

m.web.close(page)
m.hud.hide("ui_status")
