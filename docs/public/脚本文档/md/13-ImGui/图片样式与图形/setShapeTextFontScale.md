---
params: "handle: integer, scale: number"
returns: "boolean"
---

**方法名称：** 设置文本图形的字体缩放比例。

**语法：** `imgui.setShapeTextFontScale(handle, scale)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 文本图形句柄。 |
| `scale` | `number` | 是 | 相对当前内容字体的比例，必须大于 `0`。 |

| 返回值 | 说明 |
|---|---|
| `boolean` | 设置成功返回 `true`；缩放或句柄无效返回 `false`。 |

**详细说明：**

```lua
imgui.setShapeTextFontScale(textShape, 1.5)
```

缩放不会自动修改文本框尺寸，超出原 `w/h` 的内容仍会被裁剪。
