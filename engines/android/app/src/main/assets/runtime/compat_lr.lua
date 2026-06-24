local host = assert(_G._host, "native host api is not registered")

-- lr 用于逐步兼容懒人精灵。
-- 当前只映射已经由底层实现的基础能力；完整兼容要按文档逐项补齐。
local lr = {
    print = host.print,
    sleep = host.sleep,
    tap = host.touch.tap,
    swipe = host.touch.swipe,
    back = host.key.back,
    home = host.key.home,
    isRootAvailable = host.device.isRootAvailable,
    rootExec = host.root.exec,
    capture = host.screen.capture,
    getPixel = host.image.getPixel,
    getPixels = host.image.getPixels,
    releaseImage = host.image.release,
}

lr.__compat = {
    name = "lazy",
    status = "partial",
}

_G.lr = lr
