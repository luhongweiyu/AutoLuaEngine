-- 文件用途：生成可直接继续编辑的 Lua 坐标颜色表。
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
