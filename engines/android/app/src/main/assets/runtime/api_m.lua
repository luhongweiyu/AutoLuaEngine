local host = assert(_G._host, "native host api is not registered")

-- m 是 AutoLuaEngine 自己的正式脚本 API。
-- 这里只做 Lua 层命名和分组，真正的平台能力仍由 native _host 提供。
local m = {
    print = host.print,
    sleep = host.sleep,
    log = host.log,
    device = host.device,
    file = host.file,
    touch = host.touch,
    key = host.key,
    screen = host.screen,
    image = host.image,
}

-- 高频自动化能力提供 m.* 快捷入口；完整模块仍保留在 m.touch/m.image 等表中。
m.tap = host.touch.tap
m.swipe = host.touch.swipe
m.back = host.key.back
m.home = host.key.home
m.isAccessibilityEnabled = host.key.isAccessibilityEnabled
m.isRootAvailable = host.device.isRootAvailable
m.capture = host.screen.capture
m.releaseImage = host.image.release
m.getPixel = host.image.getPixel
m.getPixels = host.image.getPixels

_G.print = host.print
_G.m = m
