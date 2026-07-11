-- 文件用途：定义 AutoLuaEngine 自己的最小 m 命名空间脚本 API。
local host = assert(_G._host, "native host api is not registered")

-- 当前只暴露已经整理过的最小脚本 API。
-- Lua HostApi 只做参数转换，真实逻辑统一通过 C ABI 进入 libengine.so/core/api。
local m = {
    print = host.print,
    sleep = host.sleep,
    log = host.log,
    screen = host.screen,
    color = host.color,
}

m.capture = host.screen.capture
m.keepCapture = host.screen.keepCapture
m.releaseCapture = host.screen.releaseCapture
m.setCaptureCacheMs = host.screen.setCaptureCacheMs
m.findColors = host.color.findColors

_G.print = host.print
_G.m = m
