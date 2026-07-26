-- 生成 Lua 坐标颜色表。
--
-- context 可用字段：
--   points[]              取色点列表（第 1 个是生效基准点）
--     enabled             是否启用
--     base                是否为生效基准点
--     x, y                绝对坐标
--     dx, dy              相对基准点偏移
--     hex                 颜色，如 "FF00AA"（无 0x）
--     delta               点偏色，如 "000000"
--   region                范围（有框选用框选，否则整图）
--     left, top, right, bottom
--   image                 当前图片
--     width, height, path （临时图 path 可能为空）
--   direction             方向 1..8
--   defaultDelta          全局默认偏色，如 "000000"
--
-- 必须定义 generate(context)，返回字符串。
function generate(context)
    local lines = {"local points = {"}
    for _, point in ipairs(context.points or {}) do
        if point.enabled then
            lines[#lines + 1] = string.format(
                "    { x = %d, y = %d, color = 0x%s, delta = 0x%s },",
                point.x, point.y, point.hex, point.delta
            )
        end
    end
    lines[#lines + 1] = "}"
    return table.concat(lines, "\n")
end
