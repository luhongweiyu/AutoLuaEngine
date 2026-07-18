-- 文件用途：把统一取色上下文生成小鱼 Lua 多点找色代码。
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
    return string.format(
        "local x, y = m.findColors(%d, %d, %d, %d, %d, 0x%s, %q)\nprint(x, y)",
        range.left, range.top, range.right, range.bottom,
        context.direction, context.defaultDelta, table.concat(colors, ",")
    )
end
