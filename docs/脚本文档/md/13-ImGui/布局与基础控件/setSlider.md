---
params: "handle: integer, pos: integer"
returns: "无"
---

**方法名称：** 设置滑动条位置。

**语法：** `imgui.setSlider(handle, pos)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 滑动条句柄。 |
| `pos` | `integer` | 是 | 新位置，必须位于创建时的 `[min, max]`。 |

| 返回值 | 说明 |
|---|---|
| 无 | 本方法没有返回值；越界或句柄错误时写入错误信息。 |

**详细说明：**

```lua
imgui.setSlider(slider, 75)
```

程序设置与用户操作一样会触发 `setOnSliderEvent` 回调；重复设置当前值不会重复触发。
