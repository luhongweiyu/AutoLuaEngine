---
params: "x/y/w/h: number, text: string, colors: integer, hasBg: boolean, scale: number"
returns: "integer 或 nil"
---

**方法名称：** 创建带裁剪区域的文本图形。

**语法：** `imgui.createShapeText(x, y, w, h, text, textColor, bgColor, hasBackground, fontScale)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `x` / `y` | `number` | 是 | 文本框左上角屏幕坐标。 |
| `w` / `h` | `number` | 是 | 文本框尺寸，必须大于 `0`。 |
| `text` | `string` | 是 | UTF-8 文本。 |
| `textColor` | `integer` | 是 | 文字颜色。 |
| `bgColor` | `integer` | 是 | 背景颜色。 |
| `hasBackground` | `boolean` | 是 | 是否绘制背景。 |
| `fontScale` | `number` | 是 | 相对当前内容字体的缩放，必须大于 `0`。 |

| 返回值 | 说明 |
|---|---|
| `integer` | 成功返回文本图形句柄。 |
| `nil` | 尺寸或缩放无效。 |

**详细说明：**

```lua
local textShape = imgui.createShapeText(
    20, 20, 260, 48, "运行中", 0xFFFFFFFF, 0xAA000000, true, 1.0
)
```

超过 `w/h` 的绘制内容会被裁剪，宽度同时作为文本自动换行范围。
