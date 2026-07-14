-- 文件用途：内置示例脚本，用于验证最小 Lua API 和中文标识符支持。
print("Lua 5.4 运行正常")
print("Lua 版本 =", _VERSION)
print("系统时间戳 =", systemTime())
print("脚本运行时间 =", tickCount())

local ok = m.sleep(1000)
print("延时结果 =", ok)
print("延时后的脚本运行时间 =", tickCount())

m.log.print("m.log.print 调用正常")
print("命名空间 m/lr/cd 已就绪 =", type(m), type(lr), type(cd))

local switchOk, switchErr = useApi("lr")
if not switchOk then
    error("切换 lr API 失败：" .. tostring(switchErr))
end
print("切换 lr 后的全局 capture =", type(capture))

switchOk, switchErr = useApi("m")
if not switchOk then
    error("切换 m API 失败：" .. tostring(switchErr))
end
print("切换 m 后的全局 capture =", type(capture))

local 中文变量 = "中文标识符正常"
local function 中文函数(内容)
    print("中文函数参数 =", 内容)
    return 内容 .. " 正常"
end

print("中文变量 =", 中文变量)
print("中文函数返回 =", 中文函数("测试"))

_G.中文全局 = 123
if _G["中文" .. "全局"] ~= 123 then
    error("中文全局字段访问失败")
end
print("中文全局字段访问正常 =", _G["中文全局"])
