---
params: "handle: integer, text: string"
returns: "boolean"
---

**方法名称：** 修改文本图形的内容。

**语法：** `imgui.setShapeTextString(handle, text)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 文本图形句柄。 |
| `text` | `string` | 是 | 新的 UTF-8 文本。 |

| 返回值 | 说明 |
|---|---|
| `boolean` | 修改成功返回 `true`；句柄无效或不是文本图形返回 `false`。 |

**详细说明：**

```lua
assert(imgui.setShapeTextString(textShape, "任务已完成"))
```

文本框位置、尺寸、颜色和缩放保持不变。
