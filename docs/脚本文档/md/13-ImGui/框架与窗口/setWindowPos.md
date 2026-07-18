---
params: "handle: integer, x/y: number"
returns: "无"
---

**方法名称：** 设置内容窗口的位置。

**语法：** `imgui.setWindowPos(handle, x, y)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 窗口句柄。 |
| `x` / `y` | `number` | 是 | 相对当前 Surface 左上角的坐标，单位为物理像素。 |

| 返回值 | 说明 |
|---|---|
| 无 | 本方法没有返回值；失败原因通过 `imgui.getLastError()` 获取。 |

**详细说明：**

```lua
imgui.setWindowPos(window, 40, 120)
```

该方法只修改 Dear ImGui 内容窗口，不修改 `showWindow` 创建的外层 Android 悬浮 Surface。
