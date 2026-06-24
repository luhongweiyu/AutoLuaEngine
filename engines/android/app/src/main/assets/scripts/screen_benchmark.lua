print("screen benchmark started")

local frameCount = 10
local successCount = 0
local totalDurationMs = 0
local minDurationMs = nil
local maxDurationMs = 0
local sourceCounts = {}

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

for i = 1, frameCount do
    local img, err = m.screen.capture()
    if not img then
        print("frame", i, "capture failed =", err)
        break
    end

    recordFrame(img)

    if i == 1 then
        print("first frame size =", img.width, img.height)
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
    end

    local ok, releaseErr = m.image.release(img)
    if not ok then
        print("frame", i, "release failed =", releaseErr)
        break
    end
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
