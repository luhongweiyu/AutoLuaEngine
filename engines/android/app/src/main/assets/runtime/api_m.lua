-- 文件用途：定义 AutoLuaEngine 自己的最小 m 命名空间脚本 API。
local host = assert(_G._host, "native host api is not registered")

-- 当前只暴露已经按新边界整理过的能力。
-- 截图固定走 root 截图 C ABI：ael_root_capture(&w, &h, &pixels)。
local m = {
    print = host.print,
    sleep = host.sleep,
    log = host.log,
    root = host.root,
    screen = host.screen,
}

m.capture = host.screen.capture
m.rootCapture = host.root.screen.capture
m.keepCapture = host.screen.keepCapture
m.releaseCapture = host.screen.releaseCapture
m.setCaptureCacheMs = host.screen.setCaptureCacheMs

_G.print = host.print
_G.m = m
