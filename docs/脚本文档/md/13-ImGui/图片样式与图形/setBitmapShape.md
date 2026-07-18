---
params: "handle: integer, bitmapOrPath: object|string"
returns: "无"
---

**方法名称：** 替换位图图形的图片内容。

**语法：** `imgui.setBitmapShape(handle, bitmapOrPath)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 位图图形句柄。 |
| `bitmapOrPath` | Java Bitmap 或 `string` | 是 | 新位图对象、普通路径或 ALPKG 图片资源。 |

| 返回值 | 说明 |
|---|---|
| 无 | 本方法没有返回值；解码、对象或句柄失败时写入错误信息。 |

**详细说明：**

```lua
imgui.setBitmapShape(bitmapShape, "images/updated.png")
```

替换不会改变图形的 x、y、width、height；新图片仍缩放到原显示矩形。
