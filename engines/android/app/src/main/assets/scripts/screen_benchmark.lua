-- 文件用途：内置示例脚本，用于验证截图缓存、锁帧和点阵地址复用。
print("截图性能测试已开始")

local frameCount = 10
local successCount = 0

local cacheMs = m.setCaptureCacheMs(20)
print("截图缓存时间 =", cacheMs)

m.keepCapture()
print("已开启固定截图缓存")

local firstPixels = nil
for i = 1, frameCount do
    local width, height, pixelsOrErr = m.getScreenPixels()
    if not width then
        print("第", i, "帧截图失败 =", height)
        break
    end

    successCount = successCount + 1
    if i == 1 then
        firstPixels = pixelsOrErr
        print("第一帧尺寸 =", width, height)
        print("第一帧点阵地址 =", string.format("0x%x", pixelsOrErr))
    elseif i == 2 then
        print("第二帧点阵地址 =", string.format("0x%x", pixelsOrErr))
        print("固定缓存时地址相同 =", firstPixels == pixelsOrErr)
        m.releaseCapture()
        print("已恢复按时间缓存")
    end
end

print("性能测试成功帧数 =", successCount)
