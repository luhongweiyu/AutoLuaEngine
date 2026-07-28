-- 文件用途：定义触动精灵兼容命名空间 cd，并复用已落地的通用兼容能力。
local host = assert(_G._host, "native host api is not registered")
local m = assert(_G.m, "m api is not registered")

local cd = {
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
}

-- 触动兼容层当前与小鱼共用已经验证的通用实现，保留上面的触动专属成员优先级。
for name, value in pairs(m) do
    if cd[name] == nil then
        cd[name] = value
    end
end
-- 屏幕缩放和日志开关由扩展包装层维护，触动命名空间也必须经过同一状态。
cd.print = m.print
cd.printEx = m.printEx
cd.touchDown = m.touchDown
cd.touchMove = m.touchMove
cd.touchUp = m.touchUp

cd.__compat = {
    name = "touchsprite",
    status = "expanded",
}

_G.cd = cd
