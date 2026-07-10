-- 文件用途：内置示例脚本，用于连续截图并统计截图耗时。
print("screen benchmark started")

local frameCount = 10
local successCount = 0
local totalDurationMs = 0
local minDurationMs = nil
local maxDurationMs = 0
local sourceCounts = {}

local cacheMs = m.setCaptureCacheMs(20)
print("capture cache ms =", cacheMs)

local function recordFrame(img)
    local durationMs = tonumber(img.captureDurationMs) or 0
    local source = img.source or "unknown"

    successCount = successCount + 1
    totalDurationMs = totalDurationMs + durationMs
    if minDurationMs == nil or durationMs < minDurationMs then
        minDurationMs = durationMs
    end
    if durationMs > maxDurationMs then
        maxDurationMs = durationMs
    end
    sourceCounts[source] = (sourceCounts[source] or 0) + 1
end

local function printSourceCounts()
    for source, count in pairs(sourceCounts) do
        print("source count =", source, count)
    end
end

m.keepCapture()
print("keepCapture enabled")

for i = 1, frameCount do
    local img, err = m.screen.capture()
    if not img then
        print("frame", i, "capture failed =", err)
        break
    end

    recordFrame(img)

    if i == 1 then
        print("first frame size =", img.width, img.height)
        print("first frame id =", img.id)
        print("first frame source =", img.source)
        print("first frame durationMs =", img.captureDurationMs)

        -- 压测时只抽样少量点，确认点阵读取链路可用，不在这里实现找色算法。
        local centerX = math.floor(img.width / 2)
        local centerY = math.floor(img.height / 2)
        local colors, colorsErr = m.image.getPixels(img, {
            { x = 0, y = 0 },
            { centerX, centerY },
            { x = img.width - 1, y = img.height - 1 },
        })
        if colors then
            print("sample pixels =", #colors)
        else
            print("sample pixels failed =", colorsErr)
        end
    elseif i == 2 then
        print("second frame id =", img.id)
        m.releaseCapture()
        print("releaseCapture enabled timed cache")
    end

    -- 截图帧由引擎缓存管理，这里连续调用 capture 用于验证缓冲复用。
end

if successCount > 0 then
    local avgDurationMs = totalDurationMs / successCount
    print("benchmark success frames =", successCount)
    print("benchmark avgDurationMs =", avgDurationMs)
    print("benchmark minDurationMs =", minDurationMs)
    print("benchmark maxDurationMs =", maxDurationMs)
    printSourceCounts()
else
    print("benchmark no frame captured")
end
