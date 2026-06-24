local exportedGlobals = {}

local function clearExportedGlobals()
    for name, value in pairs(exportedGlobals) do
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

-- useApi/switchApi 只负责把某个命名空间的一层函数暴露到 _G。
-- 默认不导出 lr/cd 的函数，避免全局函数过多；需要兼容旧脚本时再显式切换。
function m.useApi(name)
    local source
    if name == "m" or name == "mine" or name == "default" then
        source = m
    elseif name == "lr" or name == "lazy" then
        source = lr
    elseif name == "cd" or name == "touchsprite" then
        source = cd
    else
        return nil, "unknown api namespace"
    end

    clearExportedGlobals()
    exportFirstLevel(source)
    return true
end

_G.useApi = m.useApi
_G.switchApi = m.useApi
_G._host = nil
