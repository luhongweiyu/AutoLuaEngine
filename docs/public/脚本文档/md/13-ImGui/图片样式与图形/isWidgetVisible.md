---
params: "handle: integer"
returns: "boolean"
---

**方法名称：** 获取控件自身的可见状态。

**语法：** `imgui.isWidgetVisible(handle)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 窗口、布局、标签页或普通控件句柄。 |

| 返回值 | 说明 |
|---|---|
| `boolean` | 自身可见标记为真时返回 `true`；隐藏或句柄无效返回 `false`。 |

**详细说明：**

```lua
local visible = imgui.isWidgetVisible(detailLayout)
imgui.setWidgetVisible(detailLayout, not visible)
```

该值只表示控件自身标记；如果父容器隐藏，子控件仍可能返回 `true`，但不会实际渲染。
