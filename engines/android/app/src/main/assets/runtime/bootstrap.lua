-- 文件用途：脚本启动时加载的 Lua 引导层，负责切换 m/lr/cd API 到全局环境。
-- 默认直接启用 m，因此普通脚本可调用 appIsFront()、runApp()、capture() 等函数。

local exportedGlobals = {}

local function clearExportedGlobals()
    for name, value in pairs(exportedGlobals) do
        -- 用户在脚本中主动覆盖同名全局变量时保留该值；API 切换只清理仍指向上一套
        -- API 成员的名称。脚本作者负责处理刻意覆盖后的调用语义。
        if rawget(_G, name) == value then
            rawset(_G, name, nil)
        end
    end
    exportedGlobals = {}
end

local function exportFirstLevel(source)
    for name, value in pairs(source) do
        if type(name) == "string"
                and name:sub(1, 2) ~= "__"
                and name ~= "useApi"
                and name ~= "switchApi" then
            rawset(_G, name, value)
            exportedGlobals[name] = value
        end
    end
end

-- useApi/switchApi 只负责把目标命名空间的一层成员导出到 _G。它不检查或保护用户
-- 自定义的同名全局变量；兼容脚本应在业务代码执行前完成切换。
function m.useApi(name)
    local source
    if name == "m" or name == "mine" or name == "default" then
        source = m
    elseif name == "lr" or name == "lazy" then
        source = lr
    elseif name == "cd" or name == "touchsprite" then
        source = cd
    else
        return nil, "未知 API 命名空间：" .. tostring(name)
    end

    clearExportedGlobals()
    exportFirstLevel(source)
    return true
end

_G.useApi = m.useApi
_G.switchApi = m.useApi

-- 默认启用小鱼精灵 API。m/lr/cd 已由前面的运行时文件创建完成。
assert(m.useApi("m"))
_G._host = nil
