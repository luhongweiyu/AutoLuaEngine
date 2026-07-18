---
params: "x/y/x2/y2: number, color: integer, filled: boolean, rounding: number"
returns: "integer 或 nil"
---

**方法名称：** 创建矩形图形。

**语法：** `imgui.createRectangle(x, y, x2, y2, color, filled, rounding)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `x` / `y` | `number` | 是 | 左上角屏幕坐标。 |
| `x2` / `y2` | `number` | 是 | 右下角屏幕坐标。 |
| `color` | `integer` | 是 | `0xAARRGGBB` 颜色。 |
| `filled` | `boolean` | 是 | `true` 填充，`false` 只绘制边框。 |
| `rounding` | `number` | 是 | 圆角半径，必须不小于 `0`。 |

| 返回值 | 说明 |
|---|---|
| `integer` | 成功返回图形句柄。 |
| `nil` | 参数无效。 |

**详细说明：**

```lua
local rect = imgui.createRectangle(20, 20, 220, 90, 0xCC167D72, true, 8)
```

图形使用当前 Surface 坐标，不属于窗口布局，也不会截获触摸。
