-- 文件用途：验证 APK 内置 OCR 模型的首次准备、重复复用、图片识别和显式释放。
local imagePath = "/sdcard/xiaoyv/scripts/ocr_builtin_probe.png"
assert(capture(imagePath))

assert(m.ocr.loadBuiltin())
assert(m.ocr.isLoaded("builtin") == true)

-- 同名同配置重复加载直接复用 session，不增加引用次数。
assert(m.ocr.loadBuiltin())

local result, readError = m.ocr.read("builtin", imagePath, {
    useAngle = true,
    maxSideLen = 1280,
    minScore = 0.5,
})
assert(result and result.items, readError or "内置 OCR 没有返回结构化结果")

local maximumScore = 0
for _, item in ipairs(result.items) do
    assert(item.score >= 0 and item.score <= 1, "OCR 置信度超出 0 到 1")
    maximumScore = math.max(maximumScore, item.score)
    print(item.text, item.x, item.y, item.w, item.h, item.score)
end
assert(#result.items > 0, "内置 OCR 没有识别到界面文字")
assert(maximumScore > 0.5, "OCR 概率被重复 softmax 或模型识别异常")
print("内置 OCR 识别条目", #result.items)

local found, findError = m.ocr.findText("builtin", imagePath, "设置", {
    exact = true,
    useAngle = true,
    minScore = 0.5,
})
assert(found and found.found, findError or "内置 OCR 找文字失败")

assert(m.ocr.release("builtin") == true)
assert(m.ocr.isLoaded("builtin") == false)
os.remove(imagePath)
