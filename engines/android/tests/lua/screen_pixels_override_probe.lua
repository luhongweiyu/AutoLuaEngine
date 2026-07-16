-- 文件用途：验证图片屏幕固定帧、图像算法读取、显式还原和脚本结束自动清理。
local testPath = "/sdcard/xiaoyv/scripts/screen_pixels_override_probe.png"
local copyPath = "/sdcard/xiaoyv/scripts/screen_pixels_override_copy.png"

-- 锁住一张物理帧，显式还原后可精确验证原宽高和点阵地址均未被图片屏幕修改。
assert(keepCapture())
local screenWidth, screenHeight, screenPixels = getScreenPixels()
assert(screenWidth and screenHeight and screenPixels, "获取原物理屏幕失败")

local testWidth = math.min(120, screenWidth)
local testHeight = math.min(80, screenHeight)
assert(capture(testPath, 0, 0, testWidth, testHeight))
assert(setScreenPixels(testPath))

local imageWidth, imageHeight, imagePixels = getScreenPixels()
assert(imageWidth == testWidth and imageHeight == testHeight, "图片屏幕尺寸不正确")
assert(imagePixels ~= screenPixels, "图片屏幕覆盖了原物理点阵")

-- 图片屏幕是固定帧，等待时间超过默认 20ms 后仍必须返回同一地址。
sleep(100)
local secondWidth, secondHeight, secondPixels = getScreenPixels()
assert(secondWidth == imageWidth and secondHeight == imageHeight, "图片屏幕被自动刷新")
assert(secondPixels == imagePixels, "图片屏幕点阵地址发生变化")

-- 模板就是当前图片屏幕本身，完整匹配应在左上角命中。
local x, y, findError = findPic(
    0,
    0,
    imageWidth - 1,
    imageHeight - 1,
    testPath,
    "000000",
    1,
    1.0
)
assert(x == 0 and y == 0, findError or "图片屏幕找图结果不正确")

-- capture 必须保存当前图片屏幕，而不是背后的物理帧。
assert(capture(copyPath))

assert(restoreScreenPixels())
local restoredWidth, restoredHeight, restoredPixels = getScreenPixels()
assert(restoredWidth == screenWidth and restoredHeight == screenHeight, "还原后的物理屏幕尺寸不正确")
assert(restoredPixels == screenPixels, "还原时没有切回原物理点阵")

-- 再次激活后不主动还原，用于验证任务统一清理路径会在脚本结束时自动释放图片屏幕。
assert(setScreenPixels(copyPath))
os.remove(testPath)
os.remove(copyPath)
print("图片屏幕探针通过；任务结束后应自动还原")
