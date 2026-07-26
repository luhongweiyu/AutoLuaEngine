-- 生成小鱼 m.findColors 的 Lua 代码。
--
-- context 可用字段：
--   points[]              取色点列表（第 1 个是生效基准点）
--     enabled             是否启用
--     base                是否为生效基准点
--     x, y                绝对坐标
--     dx, dy              相对基准点偏移（本格式主要用这个）
--     hex                 颜色，如 "FF00AA"（无 0x）
--     delta               点偏色；"000000" 时不输出
--   region                范围（有框选用框选，否则整图）
--     left, top, right, bottom
--   image                 当前图片
--     width, height, path （临时图 path 可能为空）
--   direction             方向 1..8
--   defaultDelta          全局默认偏色，如 "000000"
--
-- 必须定义 generate(context)，返回字符串。
local function point_color(point)
    local value = string.format("%d|%d|%s", point.dx, point.dy, point.hex)
    if point.delta and point.delta ~= "000000" then
        value = value .. "-" .. point.delta
    end
    return value
end

function generate(context)
    local colors = {}
    for _, point in ipairs(context.points or {}) do
        if point.enabled then colors[#colors + 1] = point_color(point) end
    end
    local range = context.region
    local s1 = string.format("local a = {point = {%d, %d}, '%s'}",context.points[1].x, context.points[1].y, table.concat(colors, ","))
    local s2 = string.format(
        "local x, y = m.findColors(%d, %d, %d, %d, %d, 0x%s, %q)\nprint(x, y)",
        range.left, range.top, range.right, range.bottom,
        context.direction, context.defaultDelta, table.concat(colors, ",")
    )
    return s1 .. "\n\n" .. s2
end
