---
params: "handle: integer"
returns: "table 或 nil"
---

**方法名称：** 获取内容窗口的位置和尺寸。

**语法：** `imgui.getWindowPos(handle)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 窗口句柄。 |

| 返回值 | 说明 |
|---|---|
| `table` | 成功返回 `{x, y, width, height}`，同时提供同名字段。 |
| `nil` | 句柄无效或不是窗口时返回 `nil`。 |

**详细说明：**

```lua
local geometry = imgui.getWindowPos(window)
if geometry then
    print(geometry.x, geometry.y, geometry.width, geometry.height)
end
```

用户拖动或缩放窗口产生的几何变化会写回模型，因此该方法读取的是当前值。
