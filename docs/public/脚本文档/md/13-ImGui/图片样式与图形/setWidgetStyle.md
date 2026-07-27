---
params: "handle: integer, style: integer, v1: number, v2: number?"
returns: "无"
---

**方法名称：** 设置单个控件的 Dear ImGui 样式变量。

**语法：** `imgui.setWidgetStyle(handle, style, v1[, v2])`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 要设置样式的控件句柄。 |
| `style` | `integer` | 是 | `imgui.StyleVar` 常量。 |
| `v1` | `number` | 是 | 浮点样式值，或二维样式的 X 分量。 |
| `v2` | `number` | 否 | 二维样式的 Y 分量；一维样式可省略。 |

| 返回值 | 说明 |
|---|---|
| 无 | 本方法没有返回值；常量、数值或句柄无效时写入错误信息。 |

**详细说明：**

```lua
imgui.setWidgetStyle(button, imgui.StyleVar.FrameRounding, 8)
imgui.setWidgetStyle(button, imgui.StyleVar.FramePadding, 16, 8)
```

样式仅在渲染该控件及其容器内容期间压栈，完成后自动恢复，不会污染其他同类控件。
