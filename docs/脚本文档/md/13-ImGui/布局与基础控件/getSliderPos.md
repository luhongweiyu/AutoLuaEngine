---
params: "handle: integer"
returns: "integer 或 nil"
---

**方法名称：** 获取滑动条当前位置。

**语法：** `imgui.getSliderPos(handle)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 滑动条句柄。 |

| 返回值 | 说明 |
|---|---|
| `integer` | 当前整数位置。 |
| `nil` | 句柄无效或不是滑动条。 |

**详细说明：**

```lua
local position = imgui.getSliderPos(slider)
print(position)
```

返回值始终位于创建时指定的范围内。
