-- 文件用途：定义小鱼精灵自己的 m 命名空间脚本 API。
local host = assert(_G._host, "native host api is not registered")

-- 固定设备能力通过 C ABI 进入 core/api；Lua 多线程直接进入 libengine.so/runtime/lua。
-- bootstrap.lua 会把 m 的一级成员统一导出为默认全局 API；本文件不能再自行写入
-- _G，否则切换 m/lr/cd API 时会出现两套不一致的全局函数来源。
local m = {
    print = host.print,
    sleep = host.sleep,
    systemTime = host.systemTime,
    tickCount = host.tickCount,
    read_alpkg_file = host.read_alpkg_file,
    touchDown = host.touchDown,
    touchMove = host.touchMove,
    touchUp = host.touchUp,
    keyDown = host.keyDown,
    keyUp = host.keyUp,
    keyPress = host.keyPress,
    inputText = host.inputText,
    getRunEnvType = host.getRunEnvType,
    appIsFront = host.appIsFront,
    appIsRunning = host.appIsRunning,
    frontAppName = host.frontAppName,
    getCurrentActivity = host.getCurrentActivity,
    runApp = host.runApp,
    stopApp = host.stopApp,
    runIntent = host.runIntent,
    installApk = host.installApk,
    getInstalledApk = host.getInstalledApk,
    getInstalledApps = host.getInstalledApps,
    getInsallAppInfos = host.getInsallAppInfos,
    getApkVerInt = host.getApkVerInt,
    exec = host.exec,
    exitScript = host.exitScript,
    getXiaoyvApi = host.getXiaoyvApi,
    getBatteryLevel = host.getBatteryLevel,
    getBoard = host.getBoard,
    getBootLoader = host.getBootLoader,
    getBrand = host.getBrand,
    getCpuAbi = host.getCpuAbi,
    getCpuAbi2 = host.getCpuAbi2,
    getCpuArch = host.getCpuArch,
    getDevice = host.getDevice,
    getDeviceId = host.getDeviceId,
    getDisplayDpi = host.getDisplayDpi,
    getDisplayInfo = host.getDisplayInfo,
    getDisplayRotate = host.getDisplayRotate,
    getDisplaySize = host.getDisplaySize,
    getFingerprint = host.getFingerprint,
    getHardware = host.getHardware,
    getId = host.getId,
    getManufacturer = host.getManufacturer,
    getModel = host.getModel,
    getNetWorkTime = host.getNetWorkTime,
    getOaid = host.getOaid,
    getOsVersionName = host.getOsVersionName,
    getPackageName = host.getPackageName,
    getProduct = host.getProduct,
    getSdPath = host.getSdPath,
    getSdkVersion = host.getSdkVersion,
    getSensorsInfo = host.getSensorsInfo,
    getSimSerialNumber = host.getSimSerialNumber,
    getSubscriberId = host.getSubscriberId,
    getWifiMac = host.getWifiMac,
    getWorkPath = host.getWorkPath,
    lockScreen = host.lockScreen,
    unLockScreen = host.unLockScreen,
    setDisplayPowerOff = host.setDisplayPowerOff,
    setAirplaneMode = host.setAirplaneMode,
    setBTEnable = host.setBTEnable,
    setWifiEnable = host.setWifiEnable,
    phoneCall = host.phoneCall,
    sendSms = host.sendSms,
    vibrate = host.vibrate,
    log = host.log,
    screen = host.screen,
    color = host.color,
    ime = host.ime,
    -- imeLib 是历史脚本使用的全局名称。它同样挂在 m 下，让默认导出和兼容 API
    -- 都由同一套切换逻辑管理。
    imeLib = host.ime,
    thread = host.thread,
    ui = host.ui,
}

m.capture = host.screen.capture
m.keepCapture = host.screen.keepCapture
m.releaseCapture = host.screen.releaseCapture
m.setCaptureCacheMs = host.screen.setCaptureCacheMs
m.findColors = host.color.findColors

-- 原生弹窗和表单。弹窗事件始终回到当前脚本线程，不会在 Android UI 线程直接执行 Lua。
local function openDialog(spec)
    return host.ui.open("dialog", spec)
end

local function waitDialog(handle)
    local event, errorMessage = host.ui.waitEvent(handle)
    -- 无论用户确认、取消还是窗口主动关闭，都释放 native 会话；对话框悬浮层已经结束时，
    -- close 只会清理 native 侧记录，不会重新创建窗口。
    host.ui.close(handle)
    if not event then
        return nil, errorMessage
    end
    if event.type == "error" then
        return nil, event.data and event.data.message or "原生界面发生错误"
    end
    return event
end

m.dialog = {}

function m.dialog.alert(title, message, buttonText)
    local handle, errorMessage = openDialog({
        type = "alert",
        title = title or "提示",
        message = message or "",
        positiveText = buttonText or "确定",
    })
    if not handle then
        return nil, errorMessage
    end
    local event, waitError = waitDialog(handle)
    if not event then
        return nil, waitError
    end
    return event.type == "confirm"
end

function m.dialog.confirm(title, message, positiveText, negativeText)
    local handle, errorMessage = openDialog({
        type = "confirm",
        title = title or "提示",
        message = message or "",
        positiveText = positiveText or "确定",
        negativeText = negativeText or "取消",
    })
    if not handle then
        return nil, errorMessage
    end
    local event, waitError = waitDialog(handle)
    if not event then
        return nil, waitError
    end
    return event.type == "confirm"
end

function m.dialog.input(title, hint, defaultText, options)
    options = options or {}
    options.type = "input"
    options.title = title or "输入"
    options.hint = hint or ""
    options.defaultText = defaultText or ""
    options.positiveText = options.positiveText or "确定"
    options.negativeText = options.negativeText or "取消"

    local handle, errorMessage = openDialog(options)
    if not handle then
        return nil, errorMessage
    end
    local event, waitError = waitDialog(handle)
    if not event then
        return nil, waitError
    end
    if event.type ~= "confirm" or not event.data then
        return nil
    end
    return event.data.value
end

function m.dialog.select(title, items, selectedIndex, options)
    options = options or {}
    options.type = "select"
    options.title = title or "选择"
    options.items = items or {}
    options.selectedIndex = selectedIndex or 1
    options.positiveText = options.positiveText or "确定"
    options.negativeText = options.negativeText or "取消"

    local handle, errorMessage = openDialog(options)
    if not handle then
        return nil, errorMessage
    end
    local event, waitError = waitDialog(handle)
    if not event then
        return nil, waitError
    end
    if event.type ~= "confirm" or not event.data then
        return nil
    end
    return event.data.index, event.data.value
end

function m.ui.form(spec)
    spec = spec or {}
    spec.type = "form"
    spec.positiveText = spec.positiveText or "确定"
    spec.negativeText = spec.negativeText or "取消"

    local handle, errorMessage = openDialog(spec)
    if not handle then
        return nil, errorMessage
    end
    local event, waitError = waitDialog(handle)
    if not event then
        return nil, waitError
    end
    if event.type ~= "confirm" or not event.data then
        return nil
    end
    return event.data.values
end

-- HUD 通过会话 ID 维护。id 是脚本侧逻辑标识，不等于 native 会话 ID。
local hudSessions = {}

m.hud = {}

function m.hud.show(id, spec)
    if id == nil then
        return nil, "HUD id 不能为空"
    end
    if hudSessions[id] then
        host.ui.close(hudSessions[id])
        hudSessions[id] = nil
    end
    local handle, errorMessage = host.ui.open("hud", spec or {})
    if handle then
        hudSessions[id] = handle
    end
    return handle, errorMessage
end

function m.hud.update(id, spec)
    local handle = hudSessions[id]
    if not handle then
        return nil, "HUD 不存在：" .. tostring(id)
    end
    return host.ui.update(handle, spec or {})
end

function m.hud.hide(id)
    local handle = hudSessions[id]
    if not handle then
        return false
    end
    hudSessions[id] = nil
    return host.ui.close(handle)
end

function m.hud.waitEvent(id, timeoutMs)
    local handle = hudSessions[id]
    if not handle then
        return nil, "HUD 不存在：" .. tostring(id)
    end
    if timeoutMs == nil then
        return host.ui.waitEvent(handle)
    end
    return host.ui.waitEvent(handle, timeoutMs)
end

-- HTML 页面支持本地文件、任意 URL 和 HTML 字符串。页面可调用 xiaoyv.emit(type, data)
-- 向脚本发送事件；脚本用 postMessage 向页面推送数据。
m.web = {}

function m.web.open(spec)
    return host.ui.open("web", spec or {})
end

function m.web.waitEvent(handle, timeoutMs)
    if timeoutMs == nil then
        return host.ui.waitEvent(handle)
    end
    return host.ui.waitEvent(handle, timeoutMs)
end

function m.web.postMessage(handle, data)
    return host.ui.postMessage(handle, data)
end

function m.web.close(handle)
    return host.ui.close(handle)
end

m.html = m.web

-- 简单提示由自动关闭的 HUD 实现，不阻塞脚本。
local toastSequence = 0
function m.toast(text, durationMs)
    toastSequence = toastSequence + 1
    return m.hud.show("__toast_" .. toastSequence, {
        text = tostring(text or ""),
        gravity = "center",
        durationMs = durationMs or 2000,
        backgroundColor = "#CC202124",
        textColor = "#FFFFFFFF",
        padding = 18,
    })
end

-- 仅注册命名空间本身。具体函数全局导出由 bootstrap.lua 统一完成。
_G.m = m
