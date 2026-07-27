---
params: "handle: integer, visible: boolean"
returns: "integer"
---

**方法名称：** 设置图形可见性。

**语法：** `imgui.setShapeVisibility(handle, visible)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 任意图形句柄。 |
| `visible` | `boolean` | 是 | `true` 显示，`false` 隐藏。 |

| 返回值 | 说明 |
|---|---|
| `integer` | 成功返回 `0`，句柄无效返回 `-1`。 |

**详细说明：**

```lua
imgui.setShapeVisibility(shape, false)
```

隐藏只停止绘制，不释放图形或图片纹理数据。
