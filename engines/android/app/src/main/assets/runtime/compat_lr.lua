-- 文件用途：定义懒人精灵兼容命名空间 lr，并复用已落地的通用兼容能力。
local host = assert(_G._host, "native host api is not registered")
local m = require("xiaoyv.runtime.compat_extended")

local lr = {
    print = host.print,
    sleep = host.sleep,
    systemTime = host.systemTime,
    tickCount = host.tickCount,
    touchDown = host.touchDown,
    touchMove = host.touchMove,
    touchUp = host.touchUp,
    keyDown = host.keyDown,
    keyUp = host.keyUp,
    keyPress = host.keyPress,
    inputText = host.inputText,
    getRunEnvType = host.getRunEnvType,
    capture = host.screen.getScreenPixels,
    keepCapture = host.screen.keepCapture,
    releaseCapture = host.screen.releaseCapture,
    setCaptureCacheMs = host.screen.setCaptureCacheMs,
    findColors = host.color.findColors,
    imeLib = host.ime,
    beginThread = host.thread.beginThread,
    Thread = {
        newThread = host.thread.newThread,
    },
}

-- 已由 compat_extended 落地、且没有懒人专属覆盖的成员直接复用同一实现，避免
-- useApi("lr") 切换后丢失加密、网络、图色、设备和节点能力。
for name, value in pairs(m) do
    if lr[name] == nil then
        lr[name] = value
    end
end
-- 这些成员在 compat_extended 中增加了缩放或日志状态，不能继续绕过包装层直连 HostApi。
lr.print = m.print
lr.printEx = m.printEx
lr.touchDown = m.touchDown
lr.touchMove = m.touchMove
lr.touchUp = m.touchUp
lr.findPic = m.__lazyFindPic

lr.__compat = {
    name = "lazy",
    status = "expanded",
}

return lr
