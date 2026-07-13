-- 文件用途：验证 ALPKG 入口、Lua 模块、包内原始资源和 HTML 页面读取。
local message = require("modules.message")

-- 包资源读取接口直接返回二进制安全的 Lua string，不经过缓存目录或包外文件系统。
local styleSheet, styleError = m.read_alpkg_file("ui/style.css")
assert(styleSheet, styleError)
assert(styleSheet:find("background", 1, true), "未读取到包内 CSS 内容")

-- Lua 加密字节码和不存在的路径不属于可读 resource，必须保持不可读取。
local luaBytes, luaError = m.read_alpkg_file("main.lua")
assert(luaBytes == nil and type(luaError) == "string", "Lua 字节码不能作为普通资源读取")

local missingBytes, missingError = m.read_alpkg_file("data/not-found.json")
assert(missingBytes == nil and type(missingError) == "string", "不存在的资源必须明确失败")

print("ALPKG 示例：" .. message.text)

local page, errorMessage = m.web.open({
    file = "ui/index.html",
    title = "ALPKG 示例",
    width = 520,
    height = 420,
})
assert(page, errorMessage)

while true do
    local event, waitError = m.web.waitEvent(page)
    if not event then
        error(waitError)
    end
    if event.type == "hello" then
        m.web.postMessage(page, { text = message.text })
    elseif event.type == "closed" then
        break
    end
end

m.web.close(page)
