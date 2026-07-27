---
params: "handle: integer"
returns: "boolean"
---

**方法名称：** 判断图形是否可见。

**语法：** `imgui.isShapeVisibility(handle)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 任意图形句柄。 |

| 返回值 | 说明 |
|---|---|
| `boolean` | 图形可见返回 `true`；隐藏或句柄无效返回 `false`。 |

**详细说明：**

```lua
if not imgui.isShapeVisibility(shape) then
    imgui.setShapeVisibility(shape, true)
end
```

需要区分“隐藏”和“句柄无效”时，再调用 `imgui.isValidHandle(handle)`。
