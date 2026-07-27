---
params: "handle: integer, bitmap: Java Bitmap"
returns: "无"
---

**方法名称：** 使用 Java `android.graphics.Bitmap` 设置图片控件。

**语法：** `imgui.setImageFromBitmap(handle, bitmap)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 图片控件句柄。 |
| `bitmap` | Java 对象 | 是 | 有效且未回收的 `android.graphics.Bitmap`。 |

| 返回值 | 说明 |
|---|---|
| 无 | 本方法没有返回值；对象或句柄无效会产生 Lua 错误或 ImGui 错误。 |

**详细说明：**

```lua
import("android.graphics.Bitmap")
import("android.graphics.Color")

local bitmap = Bitmap.createBitmap(64, 64, Bitmap.Config.ARGB_8888)
bitmap.eraseColor(Color.RED)
imgui.setImageFromBitmap(image, bitmap)
```

Bitmap 会立即复制成 RGBA 点阵，调用返回后脚本可自行管理或回收原 Java 对象。
