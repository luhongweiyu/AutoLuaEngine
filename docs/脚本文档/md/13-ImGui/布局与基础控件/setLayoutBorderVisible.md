---
params: "handle: integer, visible: boolean"
returns: "无"
---

**方法名称：** 设置布局容器是否显示边框。

**语法：** `imgui.setLayoutBorderVisible(handle, visible)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 垂直、水平或树形布局句柄。 |
| `visible` | `boolean` | 是 | `true` 显示边框，`false` 隐藏。 |

| 返回值 | 说明 |
|---|---|
| 无 | 本方法没有返回值；句柄类型不匹配时写入错误信息。 |

**详细说明：**

```lua
imgui.setLayoutBorderVisible(layout, true)
```

垂直、水平和树形布局都会显示完整子窗口边框。
