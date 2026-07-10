-- 文件用途：定义触动精灵兼容命名空间 cd；当前只挂出已重写完成的统一截图函数。
local host = assert(_G._host, "native host api is not registered")

local cd = {
    print = host.print,
    sleep = host.sleep,
    capture = host.screen.capture,
    keepCapture = host.screen.keepCapture,
    releaseCapture = host.screen.releaseCapture,
    setCaptureCacheMs = host.screen.setCaptureCacheMs,
}

cd.__compat = {
    name = "touchsprite",
    status = "minimal",
}

_G.cd = cd
