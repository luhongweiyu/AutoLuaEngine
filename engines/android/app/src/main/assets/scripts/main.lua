-- 文件用途：内置示例脚本，用于验证最小 Lua API 和中文标识符支持。
print("hello lua 5.4")
print("_VERSION =", _VERSION)

local ok = m.sleep(1000)
print("sleep result =", ok)

m.log.print("m.log.print works")
print("namespace m/lr/cd ready =", type(m), type(lr), type(cd))

local switchOk, switchErr = useApi("lr")
if not switchOk then
    error("useApi lr failed: " .. tostring(switchErr))
end
print("global capture after lr switch =", type(capture))

switchOk, switchErr = useApi("m")
if not switchOk then
    error("useApi m failed: " .. tostring(switchErr))
end
print("global capture after m switch =", type(capture))

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
