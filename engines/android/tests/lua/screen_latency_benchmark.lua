-- 文件用途：测量 Root getScreenPixels 强制刷新耗时和锁帧缓存调用开销。
import("java.lang.System")

local WARMUP_COUNT = 5
local FRESH_COUNT = 30
local CACHED_CALLS = 50000

local function nowNs()
    return System.nanoTime()
end

local function setCacheMs(ms)
    local actual, errorMessage = setCaptureCacheMs(ms)
    assert(actual ~= nil, errorMessage or "设置截图缓存失败")
    assert(actual == ms, "截图缓存返回值不正确：" .. tostring(actual))
end

local function readScreen()
    local width, height, pixelsOrError = getScreenPixels()
    if not width then
        error("截图失败：" .. tostring(height), 2)
    end
    return width, height, pixelsOrError
end

local function summarize(samples)
    table.sort(samples)
    local total = 0
    for i = 1, #samples do
        total = total + samples[i]
    end

    local function percentile(ratio)
        local index = math.ceil(#samples * ratio)
        index = math.max(1, math.min(#samples, index))
        return samples[index]
    end

    return samples[1], percentile(0.50), percentile(0.95), total / #samples, samples[#samples]
end

local function benchmark()
    assert(getRunEnvType() == 0, "截图耗时测试需要 Root 模式")

    releaseCapture()
    setCacheMs(0)

    for _ = 1, WARMUP_COUNT do
        sleep(5)
        readScreen()
    end

    local samples = {}
    local width
    local height
    for i = 1, FRESH_COUNT do
        -- cache=0 时同一毫秒内仍可能复用，等待后确保上一帧已经过期。
        sleep(5)
        local started = nowNs()
        width, height = readScreen()
        samples[i] = (nowNs() - started) / 1000000.0
    end

    local minimum, p50, p95, average, maximum = summarize(samples)
    print(string.format("截图尺寸：%dx%d", width, height))
    print(string.format(
        "强制刷新 %d 次：min=%.3fms, p50=%.3fms, p95=%.3fms, avg=%.3fms, max=%.3fms",
        FRESH_COUNT,
        minimum,
        p50,
        p95,
        average,
        maximum
    ))

    setCacheMs(60000)
    readScreen()
    keepCapture()
    local cachedStarted = nowNs()
    for _ = 1, CACHED_CALLS do
        readScreen()
    end
    local cachedTotalMs = (nowNs() - cachedStarted) / 1000000.0
    releaseCapture()

    print(string.format(
        "锁帧缓存 %d 次：total=%.3fms, avg=%.3fus/call",
        CACHED_CALLS,
        cachedTotalMs,
        cachedTotalMs * 1000.0 / CACHED_CALLS
    ))
end

local success, errorMessage = pcall(benchmark)
releaseCapture()
setCaptureCacheMs(20)

if not success then
    error(errorMessage, 0)
end
