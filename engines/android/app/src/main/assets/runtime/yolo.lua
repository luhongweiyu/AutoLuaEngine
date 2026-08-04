-- 文件用途：把 _host.yolo 的语言中立 C ABI 结果整理为正式 m.yolo Lua API。
local host = assert(_G._host, "native host api is not registered")
local native = assert(host.yolo, "native yolo api is not registered")

local DEFAULT_MODEL_NAME = "default"
local yolo = {}

local function unwrapItems(result, errorMessage)
    if not result then
        return nil, errorMessage
    end
    if type(result) ~= "table" or type(result.items) ~= "table" then
        return nil, "YOLO 返回结果缺少 items 数组"
    end
    return result.items
end

function yolo.runtimeInfo()
    return native.runtimeInfo()
end

function yolo.isAvailable()
    return native.isAvailable()
end

function yolo.load(name, labelsPath, paramPath, binPath, options)
    return native.load(name, labelsPath, paramPath, binPath, options)
end

-- 参考懒人精灵的 labels/param/bin 参数顺序，使用固定 default 模型名。
function yolo.init(labelsPath, paramPath, binPath, options)
    return native.load(DEFAULT_MODEL_NAME, labelsPath, paramPath, binPath, options)
end

function yolo.release(name)
    if name == nil then
        name = DEFAULT_MODEL_NAME
    end
    return native.release(name)
end

function yolo.isLoaded(name)
    if name == nil then
        name = DEFAULT_MODEL_NAME
    end
    return native.isLoaded(name)
end

function yolo.detectScreen(...)
    return unwrapItems(native.detectScreen(...))
end

function yolo.detectFile(name, imagePath, options)
    return unwrapItems(native.detectFile(name, imagePath, options))
end

-- 简化入口：省略 source 或直接传 options table 时检测当前屏幕，传 string 时检测图片文件。
-- GPU 是模型加载配置，不能在单次 detect 中静默切换。
function yolo.detect(source, detectOptions)
    local options = detectOptions
    if type(source) == "table" and detectOptions == nil then
        options = source
        source = nil
    end
    if source == nil then
        return yolo.detectScreen(DEFAULT_MODEL_NAME, options)
    end
    if type(source) == "string" then
        return yolo.detectFile(DEFAULT_MODEL_NAME, source, options)
    end
    return nil, "m.yolo.detect 当前只接受图片路径；省略 source 可检测当前屏幕"
end

return yolo
