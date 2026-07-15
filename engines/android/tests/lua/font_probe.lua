-- 文件用途：验证自定义点阵字库的 getFontPixel、识字和找字全链路。
m.keepCapture()

local color = "1A73E8-303030"
-- 旧项目常见的字库记录带有 "$1$0.0.xx$高度" 元数据。这里验证新字库解析器会读取
-- 最后一段高度并忽略中间元数据，而不是只支持新格式的 宽$高$点阵。
local legacyOk, legacyError = m.font.setDict(
    2,
    "1$30f0f0c30c30c000030c30c000030c30c000c3ffff03$1$0.0.58$10"
)
print("兼容旧字库", legacyOk, legacyError)

local pixel, pixelError = m.font.getFontPixel(60, 1220, 120, 1275, color)
print("生成字形点阵", pixel, pixelError)

if pixel then
    local setOk, setError = m.font.setDict(1, "脚$" .. pixel)
    print("设置字库", setOk, setError)
    local useOk, useError = m.font.useDict(1)
    print("选择字库", useOk, useError)

    if useOk then
        local result, readError = m.font.read(0, 1200, 200, 1280, color, 1.0)
        print("点阵识字", result and result.text, readError)
        print("大漠风格识字", m.font.ocr(0, 1200, 200, 1280, color, 1.0))
        print("大漠风格识字详情", m.font.ocrEx(0, 1200, 200, 1280, color, 1.0))
        local x, y = m.font.findStr(0, 1200, 200, 1280, "脚", color, 1.0)
        print("点阵找字", x, y)
        print("点阵找字全部", m.font.findStrEx(0, 1200, 200, 1280, "脚", color, 1.0))
    end
end

m.releaseCapture()
