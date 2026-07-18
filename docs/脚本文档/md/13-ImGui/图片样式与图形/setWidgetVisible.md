---
params: "handle: integer, visible: boolean"
returns: "无"
---

**方法名称：** 设置控件可见性。

**语法：** `imgui.setWidgetVisible(handle, visible)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 窗口、布局、标签页或普通控件句柄。 |
| `visible` | `boolean` | 是 | `true` 显示，`false` 隐藏。 |

| 返回值 | 说明 |
|---|---|
| 无 | 本方法没有返回值；句柄无效时写入错误信息。 |

**详细说明：**

```lua
imgui.setWidgetVisible(detailLayout, false)
```

隐藏容器会同时隐藏其全部后代，但不会销毁句柄或数据；再次设为 `true` 可恢复显示。
