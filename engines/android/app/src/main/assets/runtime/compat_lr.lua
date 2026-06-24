local host = assert(_G._host, "native host api is not registered")

-- lr 用于逐步兼容懒人精灵。
-- 当前只映射已经由底层实现的基础能力；完整兼容要按文档逐项补齐。
local lr = {
    print = host.print,
    sleep = host.sleep,
    tap = host.touch.tap,
    swipe = host.touch.swipe,
    inputText = host.input.text,
    pasteText = host.input.pasteText,
    pressKey = host.key.press,
    back = host.key.back,
    home = host.key.home,
    isRootAvailable = host.device.isRootAvailable,
    rootExec = host.root.exec,
    rootStatus = host.root.status,
    rootStat = host.root.file.stat,
    rootList = host.root.file.list,
    rootChown = host.root.file.chown,
    rootPidOf = host.root.process.pidOf,
    rootProcessList = host.root.process.list,
    rootProcessInfo = host.root.process.info,
    rootKill = host.root.process.kill,
    runApp = host.app.open,
    closeApp = host.app.stop,
    clearAppData = host.app.clearData,
    grantAppPermission = host.app.grant,
    revokeAppPermission = host.app.revoke,
    currentApp = host.app.current,
    installApp = host.app.install,
    uninstallApp = host.app.uninstall,
    disableApp = host.app.disable,
    enableApp = host.app.enable,
    isAppInstalled = host.app.isInstalled,
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
