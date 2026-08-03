-- 文件用途：补充懒人精灵/触动精灵常用的加密、网络、取色、触控、节点与工具兼容能力。
local host = assert(_G._host, "native host api is not registered")
local m = require("xiaoyv.runtime.api_m")

local function platformCall(operation, arguments)
    return host.platformCall(operation, arguments or {})
end

local function platformCallOrError(operation, arguments)
    local result, errorMessage = platformCall(operation, arguments)
    if result == nil then
        error(errorMessage or ("平台能力调用失败：" .. tostring(operation)), 3)
    end
    return result
end

-- JSON 边界不能直接承载任意二进制字符串，平台层使用 Base64，脚本 API 在这里无损还原。
local base64Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
local base64Lookup = {}
for index = 1, #base64Alphabet do
    base64Lookup[base64Alphabet:byte(index)] = index - 1
end

local function base64Encode(data)
    data = tostring(data or "")
    local output = {}
    local outputIndex = 1
    for index = 1, #data, 3 do
        local a = data:byte(index) or 0
        local b = data:byte(index + 1)
        local c = data:byte(index + 2)
        local value = (a << 16) | ((b or 0) << 8) | (c or 0)
        output[outputIndex] = base64Alphabet:sub(((value >> 18) & 0x3f) + 1, ((value >> 18) & 0x3f) + 1)
        output[outputIndex + 1] = base64Alphabet:sub(((value >> 12) & 0x3f) + 1, ((value >> 12) & 0x3f) + 1)
        output[outputIndex + 2] = b and base64Alphabet:sub(((value >> 6) & 0x3f) + 1, ((value >> 6) & 0x3f) + 1) or "="
        output[outputIndex + 3] = c and base64Alphabet:sub((value & 0x3f) + 1, (value & 0x3f) + 1) or "="
        outputIndex = outputIndex + 4
    end
    return table.concat(output)
end

local function base64Decode(data)
    data = tostring(data or ""):gsub("%s", "")
    if #data % 4 ~= 0 then
        error("Base64 数据长度无效", 3)
    end
    local output = {}
    local outputIndex = 1
    for index = 1, #data, 4 do
        local a = base64Lookup[data:byte(index)]
        local b = base64Lookup[data:byte(index + 1)]
        local third = data:byte(index + 2)
        local fourth = data:byte(index + 3)
        local c = third == 61 and 0 or base64Lookup[third]
        local d = fourth == 61 and 0 or base64Lookup[fourth]
        if a == nil or b == nil or c == nil or d == nil then
            error("Base64 数据包含无效字符", 3)
        end
        local value = (a << 18) | (b << 12) | (c << 6) | d
        output[outputIndex] = string.char((value >> 16) & 0xff)
        outputIndex = outputIndex + 1
        if third ~= 61 then
            output[outputIndex] = string.char((value >> 8) & 0xff)
            outputIndex = outputIndex + 1
        end
        if fourth ~= 61 then
            output[outputIndex] = string.char(value & 0xff)
            outputIndex = outputIndex + 1
        end
    end
    return table.concat(output)
end

-- cryptLib
local cryptLib = {}

function cryptLib.aes_crypt(data, key, operation, mode, iv, padding)
    local result = platformCallOrError("crypto.aes", {
        data = base64Encode(data),
        key = base64Encode(key),
        operation = operation,
        mode = mode or "cbc",
        iv = base64Encode(iv or ""),
        padding = padding == nil or padding == true,
    })
    return base64Decode(result)
end

function cryptLib.aes_keygen(keyLength)
    return base64Decode(platformCallOrError("crypto.aesKeygen", {
        length = keyLength,
    }))
end

function cryptLib.aes_ivgen()
    return base64Decode(platformCallOrError("crypto.aesIvgen"))
end

function cryptLib.rsa_generate_key(keyBits)
    local result = platformCallOrError("crypto.rsaGenerate", {
        bits = keyBits or 2048,
    })
    return result.publicKey, result.privateKey
end

function cryptLib.rsa_encrypt(data, key, isPublicKey)
    local result = platformCallOrError("crypto.rsaEncrypt", {
        data = base64Encode(data),
        key = key,
        publicKey = isPublicKey == true,
    })
    return base64Decode(result)
end

function cryptLib.rsa_decrypt(data, key, isPublicKey)
    local result = platformCallOrError("crypto.rsaDecrypt", {
        data = base64Encode(data),
        key = key,
        publicKey = isPublicKey == true,
    })
    return base64Decode(result)
end

m.cryptLib = cryptLib

-- 网络与 LuaSocket 兼容
local function normalizeHeaders(headers)
    if headers == nil then
        return {}
    end
    if type(headers) == "table" then
        return headers
    end
    if type(headers) ~= "string" then
        error("header 必须是 table 或 HTTP 头字符串", 3)
    end
    local result = {}
    for line in headers:gmatch("[^\r\n]+") do
        local name, value = line:match("^%s*([^:]+):%s*(.-)%s*$")
        if name then
            result[name] = value
        end
    end
    return result
end

local function networkRequest(url, method, body, timeout, headers, contentType)
    local result, errorMessage = platformCall("network.request", {
        url = url,
        method = method or "GET",
        body = base64Encode(body or ""),
        timeout = timeout or 30,
        headers = normalizeHeaders(headers),
        contentType = contentType or "application/octet-stream",
    })
    if not result then
        return nil, errorMessage
    end
    return base64Decode(result.body or ""), result.code, result.headers or {}, result.message or ""
end

function m.httpGet(url, timeout, header)
    if type(timeout) == "table" or type(timeout) == "string" then
        header, timeout = timeout, nil
    end
    return networkRequest(url, "GET", nil, timeout, header)
end

function m.httpPost(url, postdata, timeout, header)
    if type(timeout) == "table" or type(timeout) == "string" then
        header, timeout = timeout, nil
    end
    return networkRequest(
        url,
        "POST",
        postdata or "",
        timeout,
        header,
        "application/x-www-form-urlencoded; charset=utf-8"
    )
end

local function asyncHttpGetWorker(callback, url, timeout, header)
    local body, code = m.httpGet(url, timeout, header)
    callback(body, code)
end

local function asyncHttpPostWorker(callback, url, postdata, timeout, header)
    local body, code = m.httpPost(url, postdata, timeout, header)
    callback(body, code)
end

function m.asynHttpGet(callback, url, timeout, header)
    assert(type(callback) == "function", "callback 必须是函数")
    return host.thread.newThread(asyncHttpGetWorker, callback, url, timeout, header)
end

function m.asynHttpPost(callback, url, postdata, timeout, header)
    assert(type(callback) == "function", "callback 必须是函数")
    return host.thread.newThread(
        asyncHttpPostWorker,
        callback,
        url,
        postdata,
        timeout,
        header
    )
end

function m.downloadFile(url, savepath, progress)
    local result, errorMessage = platformCall("network.download", {
        url = url,
        path = savepath,
        timeout = 30,
    })
    if not result then
        return false, errorMessage
    end
    if type(progress) == "function" then
        progress(100)
    end
    return true
end

function m.uploadFile(url, uploadfile, timeout)
    local result, errorMessage = platformCall("network.upload", {
        url = url,
        path = uploadfile,
        timeout = timeout or 30,
    })
    if not result then
        return nil, errorMessage
    end
    return base64Decode(result.body or ""), result.code
end

local function webSocketEventWorker(handle, onOpened, onClosed, onError, onRecv)
    while true do
        local result = platformCallOrError("network.websocket.poll", { handle = handle })
        for _, event in ipairs(result.events or {}) do
            if event.type == "open" and type(onOpened) == "function" then
                onOpened(handle)
            elseif event.type == "close" and type(onClosed) == "function" then
                onClosed(handle, event.code, event.reason)
            elseif event.type == "error" and type(onError) == "function" then
                onError(handle, event.message, event.code)
            elseif event.type == "message" and type(onRecv) == "function" then
                onRecv(
                    handle,
                    event.binary and base64Decode(event.text or "") or (event.text or "")
                )
            end
        end
        if result.terminal then
            return
        end
        host.sleep(20)
    end
end

function m.startWebSocket(url, onOpened, onClosed, onError, onRecv)
    local handle = platformCallOrError("network.websocket.start", { url = url })
    host.thread.beginThread(
        webSocketEventWorker,
        handle,
        onOpened,
        onClosed,
        onError,
        onRecv
    )
    return handle
end

function m.closeWebSocket(handle)
    return platformCallOrError("network.websocket.close", { handle = handle })
end

function m.sendWebSocket(handle, text)
    return platformCallOrError("network.websocket.send", {
        handle = handle,
        text = tostring(text or ""),
    })
end

-- LuaSocket 的 TCP、UDP、DNS、LTN12、MIME 和 HTTP 由上游运行时模块提供。LuaSocket 本身
-- 不含 TLS；为保持既有 HTTPS 调用可用，ssl.https 和 socket.http 的 HTTPS 请求仍委托 Android
-- 网络层。HTTP 请求仍使用上游 socket.http 的完整实现。
local function isHttpsRequest(argument)
    if type(argument) == "table" and tostring(argument.scheme or ""):lower() == "https" then
        return true
    end
    local url = type(argument) == "string" and argument
            or (type(argument) == "table" and argument.url or nil)
    return type(url) == "string" and url:match("^[Hh][Tt][Tt][Pp][Ss]:") ~= nil
end

local function collectHttpsRequestBody(argument)
    local requestBody = argument.body
    if requestBody ~= nil or type(argument.source) ~= "function" then
        return requestBody
    end

    local chunks = {}
    while true do
        local chunk, sourceError = argument.source()
        if chunk == nil then
            if sourceError ~= nil then
                return nil, sourceError
            end
            break
        end
        if type(chunk) ~= "string" then
            return nil, "HTTPS request source 必须返回字符串"
        end
        chunks[#chunks + 1] = chunk
    end
    return table.concat(chunks)
end

local httpsCompat = {}

function httpsCompat.request(argument, bodyArgument)
    if type(argument) == "string" then
        if bodyArgument == nil then
            return networkRequest(argument, "GET")
        end
        argument = {
            url = argument,
            body = bodyArgument,
            method = "POST",
            headers = {
                ["content-length"] = tostring(#bodyArgument),
                ["content-type"] = "application/x-www-form-urlencoded",
            },
        }
    end

    assert(type(argument) == "table", "https.request 参数必须是 URL 或 table")
    local requestBody, requestBodyError = collectHttpsRequestBody(argument)
    if requestBodyError ~= nil then
        return nil, requestBodyError
    end
    local requestHeaders = normalizeHeaders(argument.headers)
    local body, code, headers, status = networkRequest(
        assert(argument.url, "https.request 缺少 url"),
        argument.method or (requestBody ~= nil and "POST" or "GET"),
        requestBody,
        argument.timeout,
        requestHeaders,
        requestHeaders["content-type"] or requestHeaders["Content-Type"]
    )
    if body == nil then
        return nil, code
    end
    if type(argument.sink) == "function" then
        local accepted, sinkError = argument.sink(body)
        if not accepted then
            return nil, sinkError
        end
        accepted, sinkError = argument.sink(nil)
        if not accepted then
            return nil, sinkError
        end
        return 1, code, headers, status
    end
    return body, code, headers, status
end

if package and package.preload then
    local upstreamSocketHttpLoader = assert(
        package.preload["socket.http"],
        "LuaSocket socket.http 运行时模块未注册"
    )
    package.preload["ssl.https"] = function() return httpsCompat end
    package.preload["socket.http"] = function(...)
        local socketHttp = upstreamSocketHttpLoader(...)
        local upstreamRequest = assert(socketHttp.request, "LuaSocket socket.http.request 不可用")
        socketHttp.request = function(argument, bodyArgument)
            if isHttpsRequest(argument) then
                return httpsCompat.request(argument, bodyArgument)
            end
            return upstreamRequest(argument, bodyArgument)
        end
        return socketHttp
    end
    package.preload["ffi"] = function() return m.ffi end
end

-- LuaSocket 的公开入口始终是 require("socket") / require("socket.http") / require("ssl.https")。
-- 不额外导出 m.http：它只是本文件的内部实现，不是懒人或触动的脚本 API。

-- 屏幕缩放与触控
local scale = {
    enabled = false,
    virtualWidth = 0,
    virtualHeight = 0,
    realWidth = 0,
    realHeight = 0,
}
local touchPositions = {}
-- HostApi 的按下/抬起返回值只供组合手势内部判断；公开 m.touchDown/m.touchUp
-- 继续保持无返回契约。
local rawTouchDown = host.touchDown
local rawTouchMove = host.touchMove
local rawTouchUp = host.touchUp

local function round(value)
    if value < 0 then
        return math.ceil(value - 0.5)
    end
    return math.floor(value + 0.5)
end

local function scaleAxis(value, sourceSize, targetSize)
    if sourceSize <= 1 or targetSize <= 1 then
        return round(value)
    end
    return round(value * (targetSize - 1) / (sourceSize - 1))
end

local function scaleOffset(value, sourceSize, targetSize)
    if sourceSize <= 0 or targetSize <= 0 then
        return round(value)
    end
    return round(value * targetSize / sourceSize)
end

local function physicalX(value)
    return scale.enabled and scaleAxis(value, scale.virtualWidth, scale.realWidth) or value
end

local function physicalY(value)
    return scale.enabled and scaleAxis(value, scale.virtualHeight, scale.realHeight) or value
end

local function virtualX(value)
    return scale.enabled and scaleAxis(value, scale.realWidth, scale.virtualWidth) or value
end

local function virtualY(value)
    return scale.enabled and scaleAxis(value, scale.realHeight, scale.virtualHeight) or value
end

local function physicalRadius(value)
    if not scale.enabled or value == 0 then
        return value
    end
    local factor = (
        scale.realWidth / scale.virtualWidth
        + scale.realHeight / scale.virtualHeight
    ) / 2
    return math.max(1, math.floor(value * factor + 0.5))
end

local function virtualRadius(value)
    if not scale.enabled or value == 0 then
        return value
    end
    local factor = (
        scale.virtualWidth / scale.realWidth
        + scale.virtualHeight / scale.realHeight
    ) / 2
    return math.max(1, math.floor(value * factor + 0.5))
end

local function physicalRegion(x1, y1, x2, y2)
    if x1 == 0 and y1 == 0 and x2 == 0 and y2 == 0 then
        return 0, 0, 0, 0
    end
    return physicalX(x1), physicalY(y1), physicalX(x2), physicalY(y2)
end

function m.setScreenScale(enabled, width, height)
    -- 小鱼默认 API 采用布尔开关；兼容命名空间各自处理历史参数形式。
    -- 公开操作型接口不返回内部状态。
    assert(type(enabled) == "boolean", "setScreenScale 开关只能是 true 或 false")
    if not enabled then
        scale.enabled = false
        return
    end
    assert(type(width) == "number" and width > 0, "虚拟屏幕宽度必须大于 0")
    assert(type(height) == "number" and height > 0, "虚拟屏幕高度必须大于 0")
    local realWidth, realHeight = m.getDisplaySize()
    assert(realWidth and realHeight and realWidth > 0 and realHeight > 0, "读取真实屏幕尺寸失败")
    scale.enabled = true
    scale.virtualWidth = width
    scale.virtualHeight = height
    scale.realWidth = realWidth
    scale.realHeight = realHeight
end

local function performTouchDown(id, x, y)
    local succeeded = rawTouchDown(id, physicalX(x), physicalY(y))
    if succeeded then
        touchPositions[id] = { x = x, y = y }
    else
        touchPositions[id] = nil
    end
    return succeeded
end

local function performTouchMove(id, x, y)
    local succeeded = rawTouchMove(id, physicalX(x), physicalY(y))
    if succeeded then
        touchPositions[id] = { x = x, y = y }
    end
    return succeeded
end

local function performTouchUp(id)
    touchPositions[id] = nil
    return rawTouchUp(id)
end

local function normalizeTouchPoint(idOrX, xOrY, y)
    -- 小鱼默认触控采用 Touch 风格：手指 ID 可省略，省略时使用 1。
    if y == nil then
        return 1, idOrX, xOrY
    end
    return idOrX, xOrY, y
end

function m.touchDown(idOrX, xOrY, y)
    local id, x, pointY = normalizeTouchPoint(idOrX, xOrY, y)
    performTouchDown(id, x, pointY)
end

function m.touchMove(idOrX, xOrY, y)
    local id, x, pointY = normalizeTouchPoint(idOrX, xOrY, y)
    performTouchMove(id, x, pointY)
end

function m.touchUp(idOrX, xOrY, y)
    local id, x, pointY = normalizeTouchPoint(idOrX, xOrY, y)
    performTouchMove(id, x, pointY)
    performTouchUp(id)
end

function m.tap(x, y, duration)
    -- 省略按住时长时使用常用的 30ms。
    performTouchDown(1, x, y)
    host.sleep(math.max(0, math.floor(duration == nil and 30 or duration)))
    performTouchUp(1)
end

function m.longTap(x, y, duration)
    -- longTap 同样接受时长，默认 500ms。
    performTouchDown(1, x, y)
    host.sleep(math.max(0, math.floor(duration == nil and 500 or duration)))
    performTouchUp(1)
end

local function moveTouchOverDuration(id, x, y, duration)
    duration = math.max(0, math.floor(duration or 0))
    local start = touchPositions[id] or { x = x, y = y }
    local steps = math.max(1, math.floor(duration / 16))
    local succeeded = true
    for step = 1, steps do
        local progress = step / steps
        succeeded = performTouchMove(
            id,
            math.floor(start.x + (x - start.x) * progress + 0.5),
            math.floor(start.y + (y - start.y) * progress + 0.5)
        ) and succeeded
        if step < steps then
            host.sleep(math.max(1, math.floor(duration / steps)))
        end
    end
    return succeeded
end

function m.touchMoveEx(id, x, y, duration)
    moveTouchOverDuration(id, x, y, duration)
end

function m.swipe(x1, y1, x2, y2, duration)
    performTouchDown(1, x1, y1)
    moveTouchOverDuration(1, x2, y2, duration or 300)
    performTouchUp(1)
end

-- m.ime 是小鱼默认 API 的正式输入法模块。兼容层可复用同一张 HostApi 表，
-- 但不在默认命名空间额外导出其历史模块名。
local ime = assert(m.ime, "ime is unavailable")

function ime.deleteChar()
    return platformCallOrError("ime.deleteChar")
end

function ime.finishInput()
    return platformCallOrError("ime.finishInput")
end

function ime.keyEvent(action, keyCode)
    return platformCallOrError("ime.keyEvent", {
        action = action,
        keyCode = keyCode,
    })
end

-- 取色
local colorHost = assert(host.colorCompat, "color compatibility host is unavailable")

local function colorNumber(value)
    if type(value) == "number" then
        return value & 0xffffff
    end
    assert(type(value) == "string", "颜色必须是整数或 RRGGBB 字符串")
    local normalized = value:gsub("^#", ""):gsub("^0[xX]", "")
    local result = tonumber(normalized, 16)
    assert(result ~= nil and #normalized == 6, "颜色格式必须为 RRGGBB")
    return result
end

function m.getPixelColor(x, y, kind)
    local value, errorMessage = colorHost.getPixel(physicalX(x), physicalY(y))
    if value == nil then
        return nil, errorMessage
    end
    if kind == 1 then
        return value
    end
    return string.format("%06X", value)
end

function m.getScreenPixel(x1, y1, x2, y2)
    x1, y1, x2, y2 = physicalRegion(x1, y1, x2, y2)
    return colorHost.getRegion(x1, y1, x2, y2)
end

function m.colorToRGB(color)
    local value = colorNumber(color)
    return (value >> 16) & 0xff, (value >> 8) & 0xff, value & 0xff
end

function m.colorDiff(first, second)
    local r1, g1, b1 = m.colorToRGB(first)
    local r2, g2, b2 = m.colorToRGB(second)
    return math.abs(r1 - r2) + math.abs(g1 - g2) + math.abs(b1 - b2)
end

function m.cmpColor(x, y, color, similarity)
    return colorHost.match(
        physicalX(x),
        physicalY(y),
        color,
        similarity or 1
    ) and 1 or 0
end

function m.getColorNum(x1, y1, x2, y2, color, similarity)
    x1, y1, x2, y2 = physicalRegion(x1, y1, x2, y2)
    return colorHost.count(x1, y1, x2, y2, color, similarity or 1)
end

local function scaleAbsolutePointDescription(description)
    if not scale.enabled then
        return description
    end
    local items = {}
    for item in tostring(description):gmatch("[^,]+") do
        local x, y, colors = item:match("^%s*(-?%d+)|(-?%d+)|(.+)%s*$")
        assert(x and y and colors, "多点比色格式无效：" .. item)
        items[#items + 1] = string.format(
            "%d|%d|%s",
            physicalX(tonumber(x)),
            physicalY(tonumber(y)),
            colors
        )
    end
    return table.concat(items, ",")
end

function m.cmpColorEx(description, similarity)
    return colorHost.comparePoints(
        scaleAbsolutePointDescription(description),
        similarity or 1
    ) and 1 or 0
end

function m.cmpColorExT(arguments)
    return m.cmpColorEx(table.unpack(arguments))
end

local function scaleOffsetDescription(description)
    if not scale.enabled then
        return description
    end
    local scaled = {}
    for item in tostring(description or ""):gmatch("[^,]+") do
        local x, y, colors = item:match("^%s*(-?%d+)|(-?%d+)|(.+)%s*$")
        assert(x and y and colors, "偏移颜色格式无效：" .. item)
        local px = scaleOffset(tonumber(x), scale.virtualWidth, scale.realWidth)
        local py = scaleOffset(tonumber(y), scale.virtualHeight, scale.realHeight)
        scaled[#scaled + 1] = string.format("%d|%d|%s", px, py, colors)
    end
    return table.concat(scaled, ",")
end

function m.findMultiColor(x1, y1, x2, y2, firstColor, offsetColor, direction, similarity)
    x1, y1, x2, y2 = physicalRegion(x1, y1, x2, y2)
    local x, y = colorHost.findMulti(
        x1,
        y1,
        x2,
        y2,
        firstColor,
        scaleOffsetDescription(offsetColor),
        direction or 0,
        similarity or 1,
        false
    )
    if x == nil then
        return -1, -1
    end
    return virtualX(x), virtualY(y)
end

function m.findMultiColorT(arguments)
    return m.findMultiColor(table.unpack(arguments))
end

function m.findMultiColorAll(x1, y1, x2, y2, firstColor, offsetColor, direction, similarity)
    x1, y1, x2, y2 = physicalRegion(x1, y1, x2, y2)
    local result = colorHost.findMulti(
        x1,
        y1,
        x2,
        y2,
        firstColor,
        scaleOffsetDescription(offsetColor),
        direction or 0,
        similarity or 1,
        true
    )
    for _, point in ipairs(result or {}) do
        point.x = virtualX(point.x)
        point.y = virtualY(point.y)
    end
    return result
end

function m.findMultiColorAllT(arguments)
    return m.findMultiColorAll(table.unpack(arguments))
end

function m.findColor(x1, y1, x2, y2, color, direction, similarity)
    x1, y1, x2, y2 = physicalRegion(x1, y1, x2, y2)
    local matched, x, y = colorHost.findColor(
        x1,
        y1,
        x2,
        y2,
        color,
        direction or 0,
        similarity or 1
    )
    if x == nil or x < 0 then
        return nil, -1, -1
    end
    return matched, virtualX(x), virtualY(y)
end

function m.findColorT(arguments)
    return m.findColor(table.unpack(arguments))
end

function m.isDisplayDead(x1, y1, x2, y2, seconds)
    x1, y1, x2, y2 = physicalRegion(x1, y1, x2, y2)
    local before = colorHost.regionHash(x1, y1, x2, y2)
    host.sleep(math.max(0, math.floor((seconds or 0) * 1000)))
    local after = colorHost.regionHash(x1, y1, x2, y2)
    return before ~= nil and before == after
end

function m.findCircle(x1, y1, x2, y2, dp, minDist, param1, param2, minRadius, maxRadius)
    x1, y1, x2, y2 = physicalRegion(x1, y1, x2, y2)
    local result = platformCallOrError("image.findCircle", {
        x1 = x1,
        y1 = y1,
        x2 = x2,
        y2 = y2,
        dp = dp,
        minDist = physicalRadius(minDist),
        param1 = param1,
        param2 = param2,
        minRadius = physicalRadius(minRadius),
        maxRadius = physicalRadius(maxRadius),
    })
    for _, circle in ipairs(result or {}) do
        circle.x = virtualX(circle.x)
        circle.y = virtualY(circle.y)
        circle.r = virtualRadius(circle.r)
    end
    return result
end

-- 点阵字库兼容。保留 m.ocr 的模型命名空间，同时让它可像懒人全局 ocr(...) 一样调用。
local fontCompatHost = assert(host.font, "font host is unavailable")

local function jsonQuote(value)
    value = tostring(value or "")
    value = value:gsub('[\\"%z\1-\31]', function(character)
        local replacements = {
            ['\\'] = '\\\\',
            ['"'] = '\\"',
            ['\b'] = '\\b',
            ['\f'] = '\\f',
            ['\n'] = '\\n',
            ['\r'] = '\\r',
            ['\t'] = '\\t',
        }
        return replacements[character] or string.format("\\u%04x", character:byte())
    end)
    return '"' .. value .. '"'
end

local function pointItemsJson(items, matchedText)
    local values = {}
    for index, item in ipairs(items or {}) do
        local fields = {
            '"x":' .. tostring(item.x or -1),
            '"y":' .. tostring(item.y or -1),
        }
        if matchedText ~= nil then
            fields[#fields + 1] = '"text":' .. jsonQuote(matchedText)
        elseif item.text ~= nil then
            fields[#fields + 1] = '"text":' .. jsonQuote(item.text)
            fields[#fields + 1] = '"w":' .. tostring(item.w or 0)
            fields[#fields + 1] = '"h":' .. tostring(item.h or 0)
            fields[#fields + 1] = '"score":' .. tostring(item.score or 0)
        end
        values[index] = "{" .. table.concat(fields, ",") .. "}"
    end
    return "[" .. table.concat(values, ",") .. "]"
end

local function legacyOcr(x1, y1, x2, y2, color, similarity)
    x1, y1, x2, y2 = physicalRegion(x1, y1, x2, y2)
    local result, errorMessage = fontCompatHost.ocr(
        x1, y1, x2, y2, color, similarity or 1
    )
    if result == nil then
        return nil, errorMessage
    end
    return result.text or ""
end

local function legacyOcrJson(x1, y1, x2, y2, color, similarity)
    x1, y1, x2, y2 = physicalRegion(x1, y1, x2, y2)
    local result, errorMessage = fontCompatHost.ocr(
        x1, y1, x2, y2, color, similarity or 1
    )
    if result == nil then
        return nil, errorMessage
    end
    for _, item in ipairs(result.items or {}) do
        item.x = virtualX(item.x)
        item.y = virtualY(item.y)
    end
    return pointItemsJson(result.items)
end

local function legacyFindStr(x1, y1, x2, y2, text, color, similarity)
    x1, y1, x2, y2 = physicalRegion(x1, y1, x2, y2)
    for candidate in tostring(text or ""):gmatch("[^|]+") do
        local x, y = fontCompatHost.findStr(
            x1, y1, x2, y2, candidate, color, similarity or 1
        )
        if x ~= nil then
            return candidate, virtualX(x), virtualY(y)
        end
    end
    return nil, -1, -1
end

local function legacyFindStrEx(x1, y1, x2, y2, text, color, similarity)
    x1, y1, x2, y2 = physicalRegion(x1, y1, x2, y2)
    local matches = {}
    for candidate in tostring(text or ""):gmatch("[^|]+") do
        local points = fontCompatHost.findStrEx(
            x1, y1, x2, y2, candidate, color, similarity or 1
        )
        for _, point in ipairs(points or {}) do
            matches[#matches + 1] = {
                x = virtualX(point.x),
                y = virtualY(point.y),
                text = candidate,
            }
        end
    end
    table.sort(matches, function(left, right)
        return left.y == right.y and left.x < right.x or left.y < right.y
    end)
    return pointItemsJson(matches)
end

function m.setDict(index, name)
    local ok, errorMessage = m.font.setDict(index, name)
    return ok and 1 or 0, errorMessage
end

function m.useDict(index)
    local ok, errorMessage = m.font.useDict(index)
    return ok and 1 or 0, errorMessage
end

m.findStr = legacyFindStr
m.findStrEx = legacyFindStrEx
m.ocrj = legacyOcrJson

local ocrNamespaceMetatable = getmetatable(m.ocr) or {}
ocrNamespaceMetatable.__call = function(_, ...)
    return legacyOcr(...)
end
setmetatable(m.ocr, ocrNamespaceMetatable)

function m.findStrExNew(index, ...)
    local ok, errorMessage = m.useDict(index)
    if ok ~= 1 then
        return nil, errorMessage
    end
    return legacyFindStrEx(...)
end

function m.ocrjNew(index, ...)
    local ok, errorMessage = m.useDict(index)
    if ok ~= 1 then
        return nil, errorMessage
    end
    return legacyOcrJson(...)
end

function m.ocrNew(index, ...)
    local ok, errorMessage = m.useDict(index)
    if ok ~= 1 then
        return nil, errorMessage
    end
    return legacyOcr(...)
end

-- 现有模板匹配核心复用到兼容名称；多模板返回命中的文件名和坐标。
local rawFindPic = m.findPic
local rawFindPicAll = m.findPicAll

local function imageDirection(direction)
    direction = math.tointeger(direction or 0) or 0
    -- 懒人方向：0 左上、1 中心向外、2 右下、3 左下、4 右上。
    -- 小鱼原生找图沿用更细的 1..8 扫描约定；中心向外由兼容层在全部命中中选取。
    local directions = {
        [0] = 2,
        [2] = 6,
        [3] = 7,
        [4] = 3,
    }
    assert(direction >= 0 and direction <= 4, "找图方向必须在 0 到 4 之间")
    return directions[direction] or 2
end

local function imageRegion(x1, y1, x2, y2)
    x1, y1, x2, y2 = physicalRegion(x1, y1, x2, y2)
    if x1 == 0 and y1 == 0 and x2 == 0 and y2 == 0 then
        local width, height = m.getDisplaySize()
        assert(width and height and width > 0 and height > 0, "读取真实屏幕尺寸失败")
        return 0, 0, width - 1, height - 1
    end
    return x1, y1, x2, y2
end

local function findOnePicture(x1, y1, x2, y2, name, deltaColor, direction, similarity)
    if direction ~= 1 then
        return rawFindPic(
            x1,
            y1,
            x2,
            y2,
            name,
            deltaColor or "",
            imageDirection(direction),
            similarity or 1
        )
    end

    local points, errorMessage = rawFindPicAll(
        x1,
        y1,
        x2,
        y2,
        name,
        deltaColor or "",
        imageDirection(0),
        similarity or 1
    )
    if points == nil then
        return nil, errorMessage
    end
    local centerX = (x1 + x2) / 2
    local centerY = (y1 + y2) / 2
    local nearest
    local nearestDistance
    for _, point in ipairs(points) do
        local dx = point.x - centerX
        local dy = point.y - centerY
        local distance = dx * dx + dy * dy
        if nearest == nil
                or distance < nearestDistance
                or (distance == nearestDistance and (
                    point.y < nearest.y
                    or (point.y == nearest.y and point.x < nearest.x)
                )) then
            nearest = point
            nearestDistance = distance
        end
    end
    if nearest == nil then
        return nil
    end
    return nearest.x, nearest.y
end

local function findOneOfPictures(x1, y1, x2, y2, names, deltaColor, direction, similarity)
    x1, y1, x2, y2 = imageRegion(x1, y1, x2, y2)
    for name in tostring(names):gmatch("[^|%s]+") do
        local x, y = findOnePicture(
            x1,
            y1,
            x2,
            y2,
            name,
            deltaColor or "",
            direction,
            similarity or 1
        )
        if x ~= nil then
            return name, virtualX(x), virtualY(y)
        end
    end
    return nil, -1, -1
end

function m.findPicEx(x1, y1, x2, y2, names, similarity)
    return findOneOfPictures(x1, y1, x2, y2, names, "", 0, similarity)
end

m.findImage = m.findPicEx

function m.findPicAllPoint(x1, y1, x2, y2, name, similarity)
    x1, y1, x2, y2 = imageRegion(x1, y1, x2, y2)
    local points, errorMessage = rawFindPicAll(
        x1,
        y1,
        x2,
        y2,
        name,
        "",
        imageDirection(0),
        similarity or 1
    )
    if points == nil then
        return nil, errorMessage
    end
    for _, point in ipairs(points) do
        point.x = virtualX(point.x)
        point.y = virtualY(point.y)
    end
    return points
end

function m.findPicFast(x1, y1, x2, y2, names, deltaColor, direction, similarity)
    x1, y1, x2, y2 = imageRegion(x1, y1, x2, y2)
    direction = math.tointeger(direction or 0) or 0
    local current = 0
    for name in tostring(names):gmatch("[^|%s]+") do
        local points = rawFindPicAll(
            x1,
            y1,
            x2,
            y2,
            name,
            deltaColor or "",
            imageDirection(direction == 1 and 0 or direction),
            similarity or 1
        )
        if points ~= nil and #points > 0 then
            if direction == 1 then
                local centerX = (x1 + x2) / 2
                local centerY = (y1 + y2) / 2
                table.sort(points, function(left, right)
                    local leftX = left.x - centerX
                    local leftY = left.y - centerY
                    local rightX = right.x - centerX
                    local rightY = right.y - centerY
                    local leftDistance = leftX * leftX + leftY * leftY
                    local rightDistance = rightX * rightX + rightY * rightY
                    if leftDistance ~= rightDistance then
                        return leftDistance < rightDistance
                    end
                    if left.y ~= right.y then
                        return left.y < right.y
                    end
                    return left.x < right.x
                end)
            end
            for _, point in ipairs(points) do
                point.x = virtualX(point.x)
                point.y = virtualY(point.y)
            end
            return current, points
        end
        current = current + 1
    end
    return -1, {}
end

-- useApi("lr") 用这个包装覆盖小鱼原生 findPic；默认 m.findPic 的 1..8 方向契约保持不变。
m.__lazyFindPic = findOneOfPictures

-- 设备与文件辅助
local rawPrint = m.print
local logDisabled = false

function m.print(...)
    if not logDisabled then
        return rawPrint(...)
    end
end

m.printEx = m.print

function m.setLogOff(disabled)
    logDisabled = disabled == true
end

function m.rnd(first, last)
    return math.random(first, last)
end

m.getLrApi = m.getXiaoyvApi
m.setSnapCacheTime = m.setCaptureCacheMs

local function resolveWorkPath(path)
    path = tostring(path or "")
    if path:sub(1, 1) == "/" then
        return path
    end
    local workPath = m.getWorkPath()
    if not workPath or workPath == "" then
        return path
    end
    return workPath .. "/" .. path
end

function m.getScriptVersion()
    local versionPath = resolveWorkPath("version")
    local file, openError = io.open(versionPath, "rb")
    if not file then
        error("无法读取脚本版本文件 " .. versionPath .. "：" .. tostring(openError), 2)
    end
    local text = file:read("*a")
    file:close()
    local version = math.tointeger(tonumber((text or ""):match("^%s*(.-)%s*$")))
    if version == nil then
        error("脚本版本文件必须只包含整数：" .. versionPath, 2)
    end
    return version
end

function m.playAudio(path)
    platformCallOrError("media.playAudio", { path = resolveWorkPath(path) })
end

function m.stopAudio()
    platformCallOrError("media.stopAudio")
end

function m.scanImage(path)
    platformCallOrError("media.scanImage", { path = resolveWorkPath(path) })
end

function m.checkIsDebug()
    return platformCallOrError("device.isDebug")
end

function m.zip(file, zipPath)
    platformCallOrError("file.zip", {
        source = resolveWorkPath(file),
        zip = resolveWorkPath(zipPath),
    })
end

function m.unZip(zipPath, outputDirectory, password, charset)
    platformCallOrError("file.unzip", {
        zip = resolveWorkPath(zipPath),
        output = resolveWorkPath(outputDirectory),
        password = password or "",
        charset = charset or "UTF-8",
    })
end

function m.extractApkAssets(resource, outputDirectory)
    platformCallOrError("file.extractAsset", {
        asset = resource,
        output = resolveWorkPath(outputDirectory),
    })
end

function m.extractAssets(resource, outputDirectory, pattern)
    platformCallOrError("file.extractAssetArchive", {
        asset = resource,
        output = resolveWorkPath(outputDirectory),
        pattern = pattern or "*",
    })
end

function m.setDpiToVir(dpi)
    platformCallOrError("device.setDisplayDensity", { dpi = dpi })
end

function m.setDpiToRealy()
    platformCallOrError("device.resetDisplayDensity")
end

function m.showControlBar(show)
    platformCallOrError("device.showControlBar", { show = show == true })
end

function m.restartScript()
    platformCallOrError("device.restartScript")
end

function m.setControlBarPosNew(x, y)
    platformCallOrError("device.setControlBarPosition", { x = x, y = y })
end

local function timerWorker(callback, delay, arguments)
    host.sleep(math.max(0, math.floor(delay or 0)))
    return callback(table.unpack(arguments, 1, arguments.n))
end

function m.setTimer(callback, delay, ...)
    assert(type(callback) == "function", "setTimer callback 必须是函数")
    host.thread.newThread(
        timerWorker,
        callback,
        delay,
        table.pack(...)
    )
end

function m.setRootEnvMode(enabled)
    platformCallOrError("environment.root", { enabled = enabled == true })
end

function m.setAccessibilityEnvMode()
    platformCallOrError("node.openAccessibility")
end

-- cv 页面明确列出的 Mat 截图和指针辅助。new* 返回 native userdata，其首地址保存
-- 对应 Point 或标量值，因此除本组 get/set/delete 外也可作为 ffi 的指针参数。
local cv = assert(host.cvCompat, "cv compatibility host is unavailable")

function cv.snapShot(left, top, right, bottom)
    if _G.LuaEngine == nil then
        import("com.nx.assist.lua.LuaEngine")
    end
    return _G.LuaEngine.snapShotMat(left, top, right, bottom)
end

m.cv = cv

-- 无障碍节点
local NodeMethods = {}
local NodeMetatable = { __index = NodeMethods, __name = "AccessibilityNode" }

local function nodeFromData(data)
    if data == nil then
        return nil
    end
    return setmetatable({ _data = data }, NodeMetatable)
end

local function nodeList(values)
    local result = {}
    for index, value in ipairs(values or {}) do
        result[index] = nodeFromData(value)
    end
    return result
end

function NodeMethods.id(self) return self._data.id end
function NodeMethods.text(self) return self._data.text end
function NodeMethods.desc(self) return self._data.desc end
function NodeMethods.className(self) return self._data.className end
function NodeMethods.packageName(self) return self._data.packageName end
function NodeMethods.childCount(self) return self._data.childCount end
function NodeMethods.drawingOrder(self) return self._data.drawingOrder end
function NodeMethods.depth(self) return self._data.depth end
function NodeMethods.bounds(self)
    local value = self._data.bounds
    return value.left, value.top, value.right, value.bottom
end
function NodeMethods.boundsInParent(self)
    local value = self._data.boundsInParent
    return value.left, value.top, value.right, value.bottom
end
function NodeMethods.toJson(self)
    return platformCallOrError("node.toJson", { handle = self._data.handle })
end
function NodeMethods.childs(self)
    return nodeList(platformCallOrError("node.relation", {
        handle = self._data.handle,
        relation = "children",
    }))
end
function NodeMethods.parent(self)
    local value, errorMessage = platformCall("node.relation", {
        handle = self._data.handle,
        relation = "parent",
    })
    if errorMessage then
        error(errorMessage, 2)
    end
    return nodeFromData(value)
end

local function nodeAction(self, action, arguments)
    arguments = arguments or {}
    arguments.handle = self._data.handle
    arguments.action = action
    return platformCallOrError("node.action", arguments)
end

for _, action in ipairs({
    "scrollUp", "scrollDown", "scrollLeft", "scrollRight",
    "scrollForward", "scrollBackward", "click", "longClick",
    "focus", "clearFocus", "copy", "paste", "cut", "select",
    "collapse", "expand", "contextClick",
}) do
    NodeMethods[action] = function(self)
        return nodeAction(self, action)
    end
end

function NodeMethods.setText(self, text)
    return nodeAction(self, "setText", { text = tostring(text or "") })
end
function NodeMethods.scrollTo(self, row, column)
    return nodeAction(self, "scrollTo", { row = row or 0, column = column or 0 })
end
function NodeMethods.setSelection(self, startIndex, endIndex)
    return nodeAction(self, "setSelection", { start = startIndex, ["end"] = endIndex })
end
function NodeMethods.setProgress(self, position)
    return nodeAction(self, "setProgress", { position = position })
end

local SelectorMethods = {}
local SelectorMetatable = { __index = SelectorMethods, __name = "AccessibilitySelector" }

local function selectorWithFilter(selector, field, matcher, value)
    local filters = {}
    for index, filter in ipairs(selector and selector._filters or {}) do
        filters[index] = filter
    end
    filters[#filters + 1] = { field = field, matcher = matcher, value = value }
    return setmetatable({ _filters = filters }, SelectorMetatable)
end

local stringSelectors = { "id", "text", "desc", "className", "packageName" }
for _, field in ipairs(stringSelectors) do
    m[field] = function(value)
        return selectorWithFilter(nil, field, "exact", tostring(value or ""))
    end
    SelectorMethods[field] = function(self, value)
        return selectorWithFilter(self, field, "exact", tostring(value or ""))
    end
    for suffix, matcher in pairs({
        Contains = "contains",
        StartsWith = "startsWith",
        EndsWith = "endsWith",
        Matches = "matches",
    }) do
        local name = field .. suffix
        m[name] = function(value)
            return selectorWithFilter(nil, field, matcher, tostring(value or ""))
        end
        SelectorMethods[name] = function(self, value)
            return selectorWithFilter(self, field, matcher, tostring(value or ""))
        end
    end
end

for _, field in ipairs({
    "visibleToUser", "selected", "clickable", "longClickable", "enabled",
    "password", "scrollable", "checked", "checkable", "focusable", "focused",
}) do
    m[field] = function(value)
        return selectorWithFilter(nil, field, "exact", value == true)
    end
    SelectorMethods[field] = function(self, value)
        return selectorWithFilter(self, field, "exact", value == true)
    end
end

for _, field in ipairs({ "drawingOrder", "depth", "index" }) do
    m[field] = function(value)
        return selectorWithFilter(nil, field, "exact", assert(math.tointeger(value)))
    end
    SelectorMethods[field] = function(self, value)
        return selectorWithFilter(self, field, "exact", assert(math.tointeger(value)))
    end
end

function m.bounds(left, top, right, bottom)
    return selectorWithFilter(nil, "bounds", "exact", { left, top, right, bottom })
end
function SelectorMethods.bounds(self, left, top, right, bottom)
    return selectorWithFilter(self, "bounds", "exact", { left, top, right, bottom })
end
function m.boundsInside(left, top, right, bottom)
    return selectorWithFilter(nil, "boundsInside", "exact", { left, top, right, bottom })
end
function SelectorMethods.boundsInside(self, left, top, right, bottom)
    return selectorWithFilter(self, "boundsInside", "exact", { left, top, right, bottom })
end

local function selectorQuery(self, timeout, limit)
    return platformCallOrError("node.query", {
        filters = self._filters,
        timeout = timeout or 0,
        limit = limit or 512,
    })
end

function SelectorMethods.findOne(self, timeout)
    local values = selectorQuery(self, timeout or 0, 1)
    return nodeFromData(values[1])
end
function SelectorMethods.findAll(self, timeout)
    return nodeList(selectorQuery(self, timeout or 0, 512))
end
function SelectorMethods.findOnce(self, index, timeout)
    index = math.max(0, assert(math.tointeger(index)))
    local values = selectorQuery(self, timeout or 0, index + 1)
    return nodeFromData(values[index + 1])
end
function SelectorMethods.click(self)
    return platformCallOrError("node.selectorAction", {
        filters = self._filters,
        timeout = 0,
        limit = 512,
        action = "click",
    })
end
function SelectorMethods.longClick(self)
    return platformCallOrError("node.selectorAction", {
        filters = self._filters,
        timeout = 0,
        limit = 512,
        action = "longClick",
    })
end

local nodeLib = {}
function nodeLib.getNodeXml()
    local value, errorMessage = platformCall("node.xml")
    if errorMessage then
        error(errorMessage, 2)
    end
    return value
end
function nodeLib.saveNode(path)
    return platformCallOrError("node.save", { path = path })
end
nodeLib.saveNodeNew = nodeLib.saveNode
function nodeLib.lockNode()
    return platformCallOrError("node.lock")
end
function nodeLib.unlockNode()
    return platformCallOrError("node.unlock")
end
function nodeLib.openAccessibility()
    return platformCallOrError("node.openAccessibility")
end
function nodeLib.closeAccessibility()
    return platformCallOrError("node.closeAccessibility")
end

m.nodeLib = nodeLib

return m
