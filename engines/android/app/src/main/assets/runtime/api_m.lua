local host = assert(_G._host, "native host api is not registered")

-- m 是 AutoLuaEngine 自己的正式脚本 API。
-- 这里只做 Lua 层命名和分组，真正的平台能力仍由 native _host 提供。
local m = {
    print = host.print,
    sleep = host.sleep,
    log = host.log,
    device = host.device,
    root = host.root,
    file = host.file,
    app = host.app,
    touch = host.touch,
    input = host.input,
    key = host.key,
    screen = host.screen,
    image = host.image,
}

-- 高频自动化能力提供 m.* 快捷入口；完整模块仍保留在 m.touch/m.image 等表中。
m.tap = host.touch.tap
m.swipe = host.touch.swipe
m.inputText = host.input.text
m.pasteText = host.input.pasteText
m.pressKey = host.key.press
m.back = host.key.back
m.home = host.key.home
m.isAccessibilityEnabled = host.key.isAccessibilityEnabled
m.isRootAvailable = host.device.isRootAvailable
m.setRootModeEnabled = host.device.setRootModeEnabled
m.screenState = host.device.screenState
m.wakeDevice = host.device.wake
m.sleepDevice = host.device.sleep
m.battery = host.device.battery
m.rotation = host.device.rotation
m.setRotation = host.device.setRotation
m.rootExec = host.root.exec
m.rootStatus = host.root.status
m.rootFileExists = host.root.file.exists
m.rootReadText = host.root.file.readText
m.rootWriteText = host.root.file.writeText
m.rootStat = host.root.file.stat
m.rootList = host.root.file.list
m.rootRemove = host.root.file.remove
m.rootMkdir = host.root.file.mkdir
m.rootChmod = host.root.file.chmod
m.rootChown = host.root.file.chown
m.rootPidOf = host.root.process.pidOf
m.rootProcessList = host.root.process.list
m.rootProcessInfo = host.root.process.info
m.rootKill = host.root.process.kill
m.openApp = host.app.open
m.startApp = host.app.start
m.stopApp = host.app.stop
m.clearAppData = host.app.clearData
m.grantAppPermission = host.app.grant
m.revokeAppPermission = host.app.revoke
m.currentApp = host.app.current
m.installApp = host.app.install
m.uninstallApp = host.app.uninstall
m.disableApp = host.app.disable
m.enableApp = host.app.enable
m.isAppInstalled = host.app.isInstalled
m.capture = host.screen.capture
m.releaseImage = host.image.release
m.getPixel = host.image.getPixel
m.getPixels = host.image.getPixels

_G.print = host.print
_G.m = m
