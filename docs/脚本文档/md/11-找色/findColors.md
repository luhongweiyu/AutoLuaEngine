---
params: "x1..y2: integer, dir: integer, sim: integer, colors: string"
returns: "integer, integer 或 nil, string"
---

```lua
local x, y = m.findColors(0, 0, 1079, 1919, 2, 0x101010, "0|0|FFFFFF,10|5|FF0000-101010")
if not x then
    print("找色失败：", y)
    return
end

print("找到：", x, y)
```

参数：

- `x1:integer, y1:integer, x2:integer, y2:integer`：查找范围。
- `dir:integer`：扫描方向，取值 `1` 到 `8`，沿用旧找色算法。
- `sim:integer`：默认容差，格式为 `0xRRGGBB`。
- `colors:string`：多点颜色字符串，格式为 `x|y|RRGGBB`，可用 `-RRGGBB` 给单点设置
  独立容差。

返回：

- 找到：返回 `x:integer, y:integer`。
- 未找到或失败：返回 `nil, errorMessage:string`。

说明：

- 找色直接使用当前截图缓存，不需要“是否截屏”参数。
- 如果要固定同一帧连续找色，先调用 `m.keepCapture()`；完成后调用 `m.releaseCapture()`。
