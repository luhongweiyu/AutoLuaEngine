---
params: "x/y/w/h: number, bitmapOrPath: object|string"
returns: "integer 或 nil"
---

**方法名称：** 创建位图图形。

**语法：** `imgui.createBitmapShape(x, y, width, height, bitmapOrPath)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `x` / `y` | `number` | 是 | 位图左上角屏幕坐标。 |
| `width` / `height` | `number` | 是 | 显示尺寸，必须大于 `0`。 |
| `bitmapOrPath` | Java Bitmap 或 `string` | 是 | Java 位图对象、普通路径或 ALPKG 图片资源。 |

| 返回值 | 说明 |
|---|---|
| `integer` | 成功返回位图图形句柄。 |
| `nil` | 解码、对象或参数无效。 |

**详细说明：**

```lua
local shape = imgui.createBitmapShape(20, 200, 128, 128, "images/icon.png")
assert(shape, imgui.getLastError())
```

显示尺寸与图片原始点阵尺寸相互独立，渲染时会缩放到指定矩形。
