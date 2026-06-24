print("hello lua 5.4")
print("_VERSION =", _VERSION)

local ok = sleep(1000)
print("sleep result =", ok)

log.print("log.print works")

local info = device.info()
print("device platform =", info.platform)
print("engine version =", info.engineVersion)
print("lua version =", info.luaVersion)

local path = file.appDataPath("host_api_test.txt")
local writeOk, writeErr = file.write(path, "hello from lua file api")
if not writeOk then
    print("file.write failed:", writeErr)
else
    local text, readErr = file.read(path)
    if text then
        print("file.read =", text)
    else
        print("file.read failed:", readErr)
    end

    print("file.exists after write =", file.exists(path))

    local removeOk, removeErr = file.remove(path)
    if removeOk then
        print("file.remove success")
        print("file.exists after remove =", file.exists(path))
    else
        print("file.remove failed:", removeErr)
    end
end
