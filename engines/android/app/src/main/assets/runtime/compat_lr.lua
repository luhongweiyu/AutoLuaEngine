-- 文件用途：定义懒人精灵兼容命名空间 lr；当前只挂出已完成的最小 HostApi 函数。
local host = assert(_G._host, "native host api is not registered")

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

lr.__compat = {
    name = "lazy",
    status = "minimal",
}

_G.lr = lr
