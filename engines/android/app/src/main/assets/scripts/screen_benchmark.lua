-- 文件用途：内置示例脚本，用于验证 root 截图缓存、锁帧和点阵地址复用。
print("screen benchmark started")

local frameCount = 10
local successCount = 0

local cacheMs = m.setCaptureCacheMs(20)
print("capture cache ms =", cacheMs)

m.keepCapture()
print("keepCapture enabled")

local firstPixels = nil
for i = 1, frameCount do
    local width, height, pixelsOrErr = m.capture()
    if not width then
        print("frame", i, "capture failed =", height)
        break
    end

    successCount = successCount + 1
    if i == 1 then
        firstPixels = pixelsOrErr
        print("first frame size =", width, height)
        print("first pixels =", string.format("0x%x", pixelsOrErr))
    elseif i == 2 then
        print("second pixels =", string.format("0x%x", pixelsOrErr))
        print("same pointer while keepCapture =", firstPixels == pixelsOrErr)
        m.releaseCapture()
        print("releaseCapture enabled timed cache")
    end
end

print("benchmark success frames =", successCount)
