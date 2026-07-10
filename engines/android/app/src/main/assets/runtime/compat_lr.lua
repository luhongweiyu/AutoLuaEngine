-- 文件用途：定义懒人精灵兼容命名空间 lr；当前只挂出已重写完成的截图函数。
local host = assert(_G._host, "native host api is not registered")

local lr = {
    print = host.print,
    sleep = host.sleep,
    capture = host.screen.capture,
    rootCapture = host.root.screen.capture,
    keepCapture = host.screen.keepCapture,
    releaseCapture = host.screen.releaseCapture,
    setCaptureCacheMs = host.screen.setCaptureCacheMs,
}

lr.__compat = {
    name = "lazy",
    status = "minimal",
}

_G.lr = lr
