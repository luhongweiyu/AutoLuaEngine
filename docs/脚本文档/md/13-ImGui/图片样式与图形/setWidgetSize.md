---
params: "handle: integer, width/height: number"
returns: "无"
---

**方法名称：** 设置控件尺寸。

**语法：** `imgui.setWidgetSize(handle, width, height)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 控件或布局句柄。 |
| `width` / `height` | `number` | 是 | `0` 自动计算，`-1` 使用父容器当前可用空间。 |

| 返回值 | 说明 |
|---|---|
| 无 | 本方法没有返回值；尺寸或句柄无效时写入错误信息。 |

**详细说明：**

```lua
imgui.setWidgetSize(button, 220, 56)
imgui.setWidgetSize(image, 128, 128)
```

控件只使用自身有意义的维度：滑动条和组合框使用宽度，开关使用高度，按钮、图片、表格、
输入框和布局可使用两者。窗口应优先使用 `setWindowSize`。
