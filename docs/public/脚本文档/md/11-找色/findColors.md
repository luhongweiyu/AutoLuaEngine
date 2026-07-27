---
params: "x1..y2: integer, dir: integer, sim: integer, colors: string"
returns: "integer, integer 或 nil, string"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 在当前截图缓存中执行多点找色。

**语法：** `findColors(x1, y1, x2, y2, dir, sim, colors)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `x1` | `integer` | 是 | 具体取值和组合规则见下方详细说明。 |
| `y1` | `integer` | 是 | 具体取值和组合规则见下方详细说明。 |
| `x2` | `integer` | 是 | 具体取值和组合规则见下方详细说明。 |
| `y2` | `integer` | 是 | 具体取值和组合规则见下方详细说明。 |
| `dir` | `integer` | 是 | 具体取值和组合规则见下方详细说明。 |
| `sim` | `integer` | 是 | 具体取值和组合规则见下方详细说明。 |
| `colors` | `string` | 是 | 具体取值和组合规则见下方详细说明。 |

| 返回值 | 说明 |
|---|---|
| `integer, integer 或 nil, string` | 找到时返回坐标 x、y；未找到或失败时返回 nil、错误信息。 |

**详细说明：**

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
