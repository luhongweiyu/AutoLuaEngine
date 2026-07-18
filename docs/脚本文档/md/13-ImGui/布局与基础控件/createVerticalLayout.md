---
params: "parent: integer, width/height: number?"
returns: "integer"
---

**方法名称：** 创建垂直布局容器。

**语法：** `imgui.createVerticalLayout(parent[, width[, height]])`

**参数说明：**

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|---|---|---|---|---|
| `parent` | `integer` | 是 | - | 窗口或可容纳子控件的布局句柄。 |
| `width` | `number` | 否 | `0` | `0` 自动计算，`-1` 填满可用宽度。 |
| `height` | `number` | 否 | `0` | `0` 自动计算，`-1` 填满可用高度。 |

| 返回值 | 说明 |
|---|---|
| `integer` | 成功返回布局句柄；失败返回 `0`。 |

**详细说明：**

```lua
local layout = imgui.createVerticalLayout(window, -1, -1)
imgui.createLabel(layout, "第一行", true)
imgui.createLabel(layout, "第二行", true)
```

子控件按照创建顺序从上到下排列。
