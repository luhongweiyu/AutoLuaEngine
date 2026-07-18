---
params: "x1/y1/x2/y2: number, color: integer, thickness: number"
returns: "integer 或 nil"
---

**方法名称：** 创建线段图形。

**语法：** `imgui.createLine(x1, y1, x2, y2, color, thickness)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `x1` / `y1` | `number` | 是 | 起点屏幕坐标。 |
| `x2` / `y2` | `number` | 是 | 终点屏幕坐标。 |
| `color` | `integer` | 是 | `0xAARRGGBB` 颜色。 |
| `thickness` | `number` | 是 | 线宽，必须大于 `0`。 |

| 返回值 | 说明 |
|---|---|
| `integer` | 成功返回线段句柄。 |
| `nil` | 坐标或线宽无效。 |

**详细说明：**

```lua
local line = imgui.createLine(20, 160, 260, 160, 0xFFFFFFFF, 2)
```

后续可用 `setShapePosition` 整体平移起点和终点。
