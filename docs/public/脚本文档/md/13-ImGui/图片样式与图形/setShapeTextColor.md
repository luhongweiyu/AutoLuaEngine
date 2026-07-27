---
params: "handle: integer, color: integer"
returns: "boolean"
---

**方法名称：** 修改文本图形的文字颜色。

**语法：** `imgui.setShapeTextColor(handle, color)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 文本图形句柄。 |
| `color` | `integer` | 是 | 新的 `0xAARRGGBB` 文字颜色。 |

| 返回值 | 说明 |
|---|---|
| `boolean` | 修改成功返回 `true`，句柄类型不匹配返回 `false`。 |

**详细说明：**

```lua
imgui.setShapeTextColor(textShape, 0xFFFFFF00)
```

Alpha 通道会参与最终混合，`0x00RRGGBB` 表示完全透明。
