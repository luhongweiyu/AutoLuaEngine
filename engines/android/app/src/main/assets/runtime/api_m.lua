-- 文件用途：定义 AutoLuaEngine 自己的最小 m 命名空间脚本 API。
local host = assert(_G._host, "native host api is not registered")

-- 当前只暴露已经整理过的最小脚本 API。
-- Lua HostApi 只做参数转换，真实逻辑统一通过 C ABI 进入 libengine.so/core/api。
local m = {
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
    log = host.log,
    screen = host.screen,
    color = host.color,
    ime = host.ime,
}

m.capture = host.screen.capture
m.keepCapture = host.screen.keepCapture
m.releaseCapture = host.screen.releaseCapture
m.setCaptureCacheMs = host.screen.setCaptureCacheMs
m.findColors = host.color.findColors

_G.print = host.print
_G.sleep = host.sleep
_G.systemTime = host.systemTime
_G.tickCount = host.tickCount
_G.touchDown = host.touchDown
_G.touchMove = host.touchMove
_G.touchUp = host.touchUp
_G.keyDown = host.keyDown
_G.keyUp = host.keyUp
_G.keyPress = host.keyPress
_G.inputText = host.inputText
_G.getRunEnvType = host.getRunEnvType
_G.imeLib = host.ime
_G.m = m
