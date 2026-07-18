-- 文件用途：验证当前脚本内图片屏幕与物理截图共用固定地址、图像算法读取和实时截图还原。
local testPath = "/sdcard/xiaoyv/scripts/screen_pixels_override_probe.png"
local copyPath = "/sdcard/xiaoyv/scripts/screen_pixels_override_copy.png"

-- 锁住一张物理帧，记录当前脚本任务的固定点阵地址。
assert(keepCapture())
local screenWidth, screenHeight, screenPixels = getScreenPixels()
assert(screenWidth and screenHeight and screenPixels, "获取原物理屏幕失败")

local testWidth = math.min(120, screenWidth)
local testHeight = math.min(80, screenHeight)
assert(capture(testPath, 0, 0, testWidth, testHeight))
assert(setScreenPixels(testPath))

local imageWidth, imageHeight, imagePixels = getScreenPixels()
assert(imageWidth == testWidth and imageHeight == testHeight, "图片屏幕尺寸不正确")
assert(imagePixels == screenPixels, "图片屏幕没有复用固定屏幕点阵地址")

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
assert(restoredPixels == screenPixels, "还原截图后固定点阵地址发生变化")

-- 再次激活后不主动还原，用于验证任务统一清理路径会清除图片屏幕状态。
assert(setScreenPixels(copyPath))
os.remove(testPath)
os.remove(copyPath)
print("图片屏幕探针通过；当前脚本内物理截图和图片屏幕共用固定地址")
