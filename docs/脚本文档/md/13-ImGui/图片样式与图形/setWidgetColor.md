---
params: "handle: integer, type: integer, color: integer"
returns: "无"
---

**方法名称：** 设置单个控件的 Dear ImGui 颜色。

**语法：** `imgui.setWidgetColor(handle, type, color)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 要设置颜色的控件句柄。 |
| `type` | `integer` | 是 | `imgui.Color` 常量。 |
| `color` | `integer` | 是 | `0xAARRGGBB` 颜色，也接受等价的有符号 32 位整数。 |

| 返回值 | 说明 |
|---|---|
| 无 | 本方法没有返回值；颜色类型或句柄无效时写入错误信息。 |

**详细说明：**

```lua
imgui.setWidgetColor(button, imgui.Color.Button, 0xFF167D72)
imgui.setWidgetColor(button, imgui.Color.ButtonHovered, 0xFF1B9C8A)
```

颜色只影响目标控件及其容器内容，不修改全局主题。
