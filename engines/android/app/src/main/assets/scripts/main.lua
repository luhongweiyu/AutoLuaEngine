print("hello lua 5.4")
print("_VERSION =", _VERSION)

local ok = m.sleep(1000)
print("sleep result =", ok)

m.log.print("m.log.print works")

local info = m.device.info()
print("device platform =", info.platform)
print("engine version =", info.engineVersion)
print("lua version =", info.luaVersion)

local path = m.file.appDataPath("host_api_test.txt")
local writeOk, writeErr = m.file.write(path, "hello from lua file api")
if not writeOk then
    print("file.write failed:", writeErr)
else
    local text, readErr = m.file.read(path)
    if text then
        print("file.read =", text)
    else
        print("file.read failed:", readErr)
    end

    print("file.exists after write =", m.file.exists(path))

    local removeOk, removeErr = m.file.remove(path)
    if removeOk then
        print("file.remove success")
        print("file.exists after remove =", m.file.exists(path))
    else
        print("file.remove failed:", removeErr)
    end
end

print("namespace m/lr/cd ready =", type(m), type(lr), type(cd))
local switchOk, switchErr = useApi("lr")
if not switchOk then
    error("useApi lr failed: " .. tostring(switchErr))
end
print("global tap after lr switch =", type(tap))
switchOk, switchErr = useApi("m")
if not switchOk then
    error("useApi m failed: " .. tostring(switchErr))
end
print("global tap after m switch =", type(tap))

local 中文变量 = "中文标识符正常"
local function 中文函数(内容)
    print("中文函数参数 =", 内容)
    return 内容 .. " OK"
end

print("中文变量 =", 中文变量)
print("中文函数返回 =", 中文函数("测试"))

_G.中文全局 = 123
if _G["中文" .. "全局"] ~= 123 then
    error("中文全局字段访问失败")
end
print("中文全局字段访问正常 =", _G["中文全局"])
