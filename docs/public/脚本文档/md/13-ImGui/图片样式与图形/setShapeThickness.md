---
params: "handle: integer, thickness: number"
returns: "boolean"
---

**方法名称：** 设置图形边框或线段粗细。

**语法：** `imgui.setShapeThickness(handle, thickness)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 任意图形句柄。 |
| `thickness` | `number` | 是 | 新线宽，必须大于 `0`。 |

| 返回值 | 说明 |
|---|---|
| `boolean` | 设置成功返回 `true`；参数或句柄无效返回 `false`。 |

**详细说明：**

```lua
imgui.setShapeThickness(line, 4)
imgui.setShapeThickness(rectangle, 3)
```

线宽对线段、非填充矩形、非填充圆和非填充多边形可见；位图和文本图形不支持该方法并
返回 `false`。
