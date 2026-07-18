---
params: "parent: integer, title: string?, color: integer?, width/height: number?"
returns: "integer 或 nil"
---

**方法名称：** 创建带 Alpha 通道的颜色选择器。

**语法：** `imgui.createColorPicker(parent[, title[, color[, width[, height]]]])`

**参数说明：**

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|---|---|---|---|---|
| `parent` | `integer` | 是 | - | 父容器句柄。 |
| `title` | `string` | 否 | `"Color"` | 控件标题和内部标识。 |
| `color` | `integer` | 否 | `0xFF000000` | 初始颜色，格式为 `0xAARRGGBB`。 |
| `width` | `number` | 否 | `0` | 控件宽度。 |
| `height` | `number` | 否 | `0` | 控件占用高度。 |

| 返回值 | 说明 |
|---|---|
| `integer` | 成功返回颜色选择器句柄。 |
| `nil` | 参数或父句柄无效。 |

**详细说明：**

```lua
local picker = imgui.createColorPicker(parent, "主题色", 0xFF167D72, -1, 240)
assert(picker, imgui.getLastError())
```

用户选择的颜色保存在控件模型中；本方法保持懒人精灵现有 API 范围，不额外定义读取回调。
