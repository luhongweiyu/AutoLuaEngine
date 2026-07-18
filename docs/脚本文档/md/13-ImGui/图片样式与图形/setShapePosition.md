---
params: "handle: integer, x/y: number"
returns: "integer"
---

**方法名称：** 设置图形位置。

**语法：** `imgui.setShapePosition(handle, x, y)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 任意图形句柄。 |
| `x` / `y` | `number` | 是 | 新的基准坐标。 |

| 返回值 | 说明 |
|---|---|
| `integer` | 成功返回 `0`，句柄或坐标无效返回 `-1`。 |

**详细说明：**

```lua
assert(imgui.setShapePosition(shape, 100, 240) == 0)
```

矩形和线段会保持原尺寸并整体平移；多边形会平移所有顶点；圆形、位图和文字修改其左上角
或圆心基准坐标。
