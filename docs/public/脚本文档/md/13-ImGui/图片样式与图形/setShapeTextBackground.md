---
params: "handle: integer, color: integer, hasBackground: boolean"
returns: "boolean"
---

**方法名称：** 修改文本图形背景颜色和显示状态。

**语法：** `imgui.setShapeTextBackground(handle, color, hasBackground)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 文本图形句柄。 |
| `color` | `integer` | 是 | 新的 `0xAARRGGBB` 背景颜色。 |
| `hasBackground` | `boolean` | 是 | 是否绘制背景矩形。 |

| 返回值 | 说明 |
|---|---|
| `boolean` | 修改成功返回 `true`，句柄类型不匹配返回 `false`。 |

**详细说明：**

```lua
imgui.setShapeTextBackground(textShape, 0xAA000000, true)
```

关闭背景后颜色值仍会保存，再次开启时继续使用最近设置的颜色。
