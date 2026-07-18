---
params: "x/y/radius: number, color: integer, filled: boolean, segments: integer"
returns: "integer 或 nil"
---

**方法名称：** 创建圆形图形。

**语法：** `imgui.createCircle(x, y, radius, color, filled, segments)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `x` / `y` | `number` | 是 | 圆心屏幕坐标。 |
| `radius` | `number` | 是 | 半径，必须大于 `0`。 |
| `color` | `integer` | 是 | `0xAARRGGBB` 颜色。 |
| `filled` | `boolean` | 是 | 是否填充。 |
| `segments` | `integer` | 是 | 圆周分段数，至少为 `3`；常用 `12..64`。 |

| 返回值 | 说明 |
|---|---|
| `integer` | 成功返回图形句柄。 |
| `nil` | 参数无效。 |

**详细说明：**

```lua
local circle = imgui.createCircle(160, 120, 40, 0xFFFFD166, false, 32)
```

分段数越高边缘越平滑，同时增加少量绘制开销。
