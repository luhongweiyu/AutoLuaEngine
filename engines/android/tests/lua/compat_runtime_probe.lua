-- Android compatibility API runtime smoke test.
--
-- Run this file through script.run with workPath set to a writable directory.
-- It writes tab-separated results to compat_runtime_probe_result.txt and verifies
-- the script-end callback with compat_runtime_probe_exit.txt.
-- Root mode and the accessibility service must already be active; network time is
-- treated as optional because the test environment may block outbound NTP.

local workPath = assert(getWorkPath(), "getWorkPath unavailable")
assert(workPath ~= "", "workPath is empty")

local resultPath = workPath .. "/compat_runtime_probe_result.txt"
local exitPath = workPath .. "/compat_runtime_probe_exit.txt"
local runId = tostring(systemTime())
local results = {}
local failures = 0

local function flatten(value)
    local text = tostring(value == nil and "ok" or value)
    text = text:gsub("[\r\n\t]+", " ")
    text = text:gsub("%s%s+", " ")
    return text
end

local function writeText(path, text)
    local file = assert(io.open(path, "wb"))
    file:write(text)
    file:close()
end

local function readText(path)
    local file = assert(io.open(path, "rb"))
    local text = file:read("*a")
    file:close()
    return text
end

local function record(name, status, detail)
    results[#results + 1] = table.concat({
        name,
        status,
        flatten(detail),
    }, "\t")
end

local function traceback(message)
    if debug and debug.traceback then
        return debug.traceback(tostring(message), 2)
    end
    return tostring(message)
end

local function test(name, callback)
    local ok, detail = xpcall(callback, traceback)
    if ok then
        record(name, "PASS", detail)
    else
        failures = failures + 1
        record(name, "FAIL", detail)
    end
end

local function optional(name, callback)
    local ok, detail = xpcall(callback, traceback)
    record(name, ok and "PASS" or "SKIP", detail)
end

os.remove(resultPath)
os.remove(exitPath)

setStopCallBack(function(hasError, exitCode)
    writeText(exitPath, tostring(hasError) .. "\t" .. tostring(exitCode))
end)

test("lua-5.4-and-api-presence", function()
    assert(_VERSION == "Lua 5.4", "unexpected Lua version: " .. tostring(_VERSION))
    assert(type(m) == "table")
    assert(type(cryptLib) == "table")
    assert(type(httpGet) == "function")
    assert(type(cv) == "table")
    assert(type(nodeLib) == "table")
    assert(type(ime) == "table")
    assert(m.ime == ime)
    assert(type(m.ime.lock) == "function")
    assert(type(m.ime.setText) == "function")
    assert(type(m.ime.unlock) == "function")
    assert(type(m.ime.deleteChar) == "function")
    assert(type(m.ime.finishInput) == "function")
    assert(type(m.ime.keyEvent) == "function")
    assert(m.imeLib == nil)
    assert(imeLib == nil)
    assert(m.http == nil)
    return _VERSION
end)

test("lua-file-io", function()
    local path = workPath .. "/compat-file-" .. runId .. ".txt"
    writeText(path, "xiaoyv-file-ok")
    local value = readText(path)
    os.remove(path)
    assert(value == "xiaoyv-file-ok")
    return value
end)

test("cffi-native-abi", function()
    local ffi = require("ffi")
    assert(ffi == m.ffi, "require(ffi) did not return m.ffi")
    assert(type(ffi.cdef) == "function")
    assert(type(ffi.load) == "function")
    assert(ffi.abi("le"))

    ffi.cdef[[
        typedef struct {
            int sequence;
            double score;
        } xiaoyv_ffi_probe_pair;

        int getpid(void);
        size_t strlen(const char *text);
        double strtod(const char *text, char **end);
        int snprintf(char *buffer, size_t size, const char *format, ...);
    ]]

    local pair = ffi.new("xiaoyv_ffi_probe_pair")
    pair.sequence = 7
    pair.score = 3.5
    assert(pair.sequence == 7 and pair.score == 3.5)
    assert(ffi.sizeof(pair) >= 16)

    assert(ffi.tonumber(ffi.C.getpid()) > 0)
    local libc = ffi.load("c")
    assert(ffi.tonumber(libc.getpid()) > 0)
    assert(ffi.tonumber(libc.strlen("xiaoyv")) == 6)
    assert(math.abs(ffi.tonumber(libc.strtod("3.25", nil)) - 3.25) < 0.000001)

    local buffer = ffi.new("char[32]")
    assert(libc.snprintf(buffer, ffi.sizeof(buffer), "%d", ffi.new("int", 42)) == 2)
    assert(ffi.string(buffer) == "42")

    local callback = ffi.cast("int (*)(int)", function(value)
        return value + 1
    end)
    assert(ffi.tonumber(callback(41)) == 42)
    callback:free()
    return "struct+float+vararg+callback"
end)

test("aes-round-trip", function()
    local key = cryptLib.aes_keygen(32)
    local iv = cryptLib.aes_ivgen()
    assert(#key == 32)
    assert(#iv == 16)
    local encrypted = cryptLib.aes_crypt(
        "xiaoyv\0aes",
        key,
        "encrypt",
        "cbc",
        iv,
        true
    )
    local plain = cryptLib.aes_crypt(
        encrypted,
        key,
        "decrypt",
        "cbc",
        iv,
        true
    )
    assert(plain == "xiaoyv\0aes")
    return "cipher-bytes=" .. #encrypted
end)

test("rsa-round-trip", function()
    local publicKey, privateKey = cryptLib.rsa_generate_key(1024)
    assert(publicKey:find("BEGIN PUBLIC KEY", 1, true))
    assert(privateKey:find("BEGIN PRIVATE KEY", 1, true))
    local encrypted = cryptLib.rsa_encrypt("xiaoyv-rsa", publicKey, true)
    local plain = cryptLib.rsa_decrypt(encrypted, privateKey, false)
    assert(plain == "xiaoyv-rsa")
    return "cipher-bytes=" .. #encrypted
end)

test("clipboard-round-trip", function()
    local previous, readError = readPasteboard()
    assert(previous ~= nil, readError)
    local marker = "xiaoyv-clipboard-" .. runId
    writePasteboard(marker)
    local current, currentError = readPasteboard()
    writePasteboard(previous)
    assert(current ~= nil, currentError)
    assert(current == marker, "clipboard mismatch")
    return "restored"
end)

local healthUrl = "http://127.0.0.1:18380/health"

test("http-get", function()
    local body, code, headers, message = httpGet(healthUrl, 5)
    assert(body, code)
    assert(code == 200, "HTTP " .. tostring(code))
    assert(body:find('"ok":true', 1, true), body)
    assert(type(headers) == "table")
    return message
end)

test("luasocket-core-tcp-and-udp", function()
    local socket = require("socket")
    assert(type(socket.tcp) == "function")
    assert(type(socket.udp) == "function")
    assert(type(socket.select) == "function")
    assert(type(socket.dns) == "table")

    local tcp = assert(socket.tcp())
    assert(tcp:settimeout(5))
    assert(tcp:connect("127.0.0.1", 18380))
    assert(tcp:send("GET /health HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"))
    local response = {}
    while true do
        local chunk, receiveError, partial = tcp:receive(1024)
        if chunk and #chunk > 0 then
            response[#response + 1] = chunk
        end
        if partial and #partial > 0 then
            response[#response + 1] = partial
        end
        if receiveError == "closed" then
            break
        end
        assert(receiveError == nil, receiveError)
    end
    tcp:close()
    assert(table.concat(response):find('"ok":true', 1, true))

    local receiver = assert(socket.udp())
    assert(receiver:setsockname("127.0.0.1", 0))
    local _, receiverPort = assert(receiver:getsockname())
    assert(receiver:settimeout(2))
    local sender = assert(socket.udp())
    assert(sender:sendto("xiaoyv-luasocket-udp", "127.0.0.1", receiverPort))
    local payload = assert(receiver:receivefrom())
    sender:close()
    receiver:close()
    assert(payload == "xiaoyv-luasocket-udp")
    return "tcp+udp"
end)

test("luasocket-http-request", function()
    local http = require("socket.http")
    local body, code = http.request(healthUrl)
    assert(body, code)
    assert(code == 200, "HTTP " .. tostring(code))
    assert(body:find('"ok":true', 1, true), body)
    return "HTTP " .. code
end)

test("luasocket-mime-and-https-compat", function()
    local mime = require("mime")
    local https = require("ssl.https")
    assert(mime.b64("xiaoyv") == "eGlhb3l2")
    assert(type(https.request) == "function")
    return "mime+https"
end)

test("ltn12-pump", function()
    local ltn12 = require("ltn12")
    local target = {}
    local sink = ltn12.sink.table(target)
    local source = ltn12.source.string("xiaoyv-ltn12")
    local ok, pumpError = ltn12.pump.all(source, sink)
    assert(ok, pumpError)
    assert(table.concat(target) == "xiaoyv-ltn12")
    return table.concat(target)
end)

test("download-file", function()
    local path = workPath .. "/compat-download-" .. runId .. ".json"
    local ok, downloadError = downloadFile(healthUrl, path)
    assert(ok, downloadError)
    local body = readText(path)
    os.remove(path)
    assert(body:find('"ok":true', 1, true), body)
    return #body .. " bytes"
end)

test("async-http-get", function()
    local path = workPath .. "/compat-async-http-" .. runId .. ".txt"
    os.remove(path)
    asynHttpGet(function(body, code)
        writeText(path, tostring(code) .. "\n" .. tostring(body or ""))
    end, healthUrl, 5)

    for _ = 1, 40 do
        local file = io.open(path, "rb")
        if file then
            file:close()
            break
        end
        sleep(50)
    end

    local value = readText(path)
    os.remove(path)
    assert(value:find("^200"))
    assert(value:find('"ok":true', 1, true), value)
    return "callback"
end)

test("zip-round-trip", function()
    local sourceName = "compat-zip-source-" .. runId .. ".txt"
    local zipName = "compat-zip-" .. runId .. ".zip"
    local outputName = "compat-unzip-" .. runId
    local sourcePath = workPath .. "/" .. sourceName
    local zipPath = workPath .. "/" .. zipName
    local outputPath = workPath .. "/" .. outputName
    local extractedPath = outputPath .. "/" .. sourceName

    writeText(sourcePath, "xiaoyv-zip-ok")
    zip(sourceName, zipName)
    unZip(zipName, outputName)
    local value = readText(extractedPath)

    os.remove(extractedPath)
    os.remove(outputPath)
    os.remove(zipPath)
    os.remove(sourcePath)

    assert(value == "xiaoyv-zip-ok")
    return value
end)

test("timer-callback", function()
    local path = workPath .. "/compat-timer-" .. runId .. ".txt"
    os.remove(path)
    setTimer(function(targetPath, value)
        writeText(targetPath, value)
    end, 100, path, "xiaoyv-timer-ok")
    sleep(400)
    local value = readText(path)
    os.remove(path)
    assert(value == "xiaoyv-timer-ok")
    return value
end)

test("device-info", function()
    local model = getModel()
    local abi = getCpuAbi()
    local arch = getCpuArch()
    local packageName = getPackageName()
    assert(type(model) == "string" and model ~= "")
    assert(type(abi) == "string" and abi ~= "")
    assert(type(arch) == "number")
    assert(packageName == "com.xiaoyv.engine")
    assert(checkIsDebug() == true)
    return table.concat({ model, abi, tostring(arch) }, ",")
end)

test("cv-native-values", function()
    local point = cv.newPoint(10, 20)
    cv.setPoint(point, 30, 40)
    local value = cv.getPoint(point)
    assert(value.x == 30 and value.y == 40)
    cv.deletePtr(point)

    local scalar = cv.newDouble(1.25)
    assert(math.abs(cv.getDouble(scalar) - 1.25) < 0.000001)
    cv.setDouble(scalar, 2.5)
    assert(math.abs(cv.getDouble(scalar) - 2.5) < 0.000001)
    cv.deletePtr(scalar)
    return "point=30,40 double=2.5"
end)

test("screen-and-color", function()
    local width, height, pixels = getScreenPixel(0, 0, 15, 15)
    assert(width == 16 and height == 16)
    assert(type(pixels) == "table" and #pixels == width * height)
    local color = getPixelColor(1, 1)
    assert(type(color) == "string" and #color == 6)
    return color
end)

test("opencv-mat-and-circle", function()
    local mat = cv.snapShot(0, 0, 32, 32)
    assert(mat ~= nil, "cv.snapShot returned nil")
    assert(mat:cols() == 32 and mat:rows() == 32)
    mat:release()

    local circles = findCircle(0, 0, 64, 64, 1, 10, 100, 30, 3, 24)
    assert(type(circles) == "table")
    return "circles=" .. #circles
end)

test("accessibility-node-query", function()
    local xml = nodeLib.getNodeXml()
    assert(type(xml) == "string" and #xml > 0)
    assert(nodeLib.lockNode() == true)
    local nodes = packageName("com.xiaoyv.engine"):findAll(1000)
    nodeLib.unlockNode()
    assert(type(nodes) == "table" and #nodes > 0)
    return "nodes=" .. #nodes .. " xml-bytes=" .. #xml
end)

test("api-namespace-switch", function()
    assert(useApi("lr") == true)
    assert(type(getLrApi) == "function")
    assert(useApi("cd") == true)
    assert(type(getXiaoyvApi) == "function")
    assert(useApi("m") == true)
    return "lr,cd,m"
end)

test("touch-and-screen-scale", function()
    local legacyScaleAccepted = pcall(setScreenScale, 1, 360, 640)
    local scale = table.pack(setScreenScale(true, 360, 640))
    local down = table.pack(touchDown(358, 300))
    sleep(20)
    local moved = table.pack(touchMove(359, 301))
    local up = table.pack(touchUp(359, 301))
    local reset = table.pack(setScreenScale(false))

    local indexedDown = table.pack(touchDown(4, 358, 300))
    sleep(20)
    local indexedMove = table.pack(touchMove(4, 359, 301))
    local indexedUp = table.pack(touchUp(4, 359, 301))
    local tapResult = table.pack(tap(358, 300, 20))
    local longTapResult = table.pack(longTap(358, 300, 20))
    local swipeResult = table.pack(swipe(716, 602, 718, 604, 32))

    assert(not legacyScaleAccepted, "default m API must require a boolean screen-scale switch")
    assert(scale.n == 0, "setScreenScale must not return a value")
    assert(down.n == 0, "touchDown must not return a value")
    assert(moved.n == 0, "touchMove must not return a value")
    assert(up.n == 0, "touchUp must not return a value")
    assert(reset.n == 0, "setScreenScale reset must not return a value")
    assert(indexedDown.n == 0 and indexedMove.n == 0 and indexedUp.n == 0)
    assert(tapResult.n == 0 and longTapResult.n == 0 and swipeResult.n == 0)
    return "scaled-touch-ok"
end)

optional("network-time", function()
    local value = getNetWorkTime()
    assert(type(value) == "string" and value ~= "", "NTP unavailable")
    return value
end)

writeText(resultPath, table.concat(results, "\n") .. "\n")

if failures > 0 then
    error("compatibility runtime probe failures: " .. failures)
end
