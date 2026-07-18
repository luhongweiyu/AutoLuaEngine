---
params: "handle: integer, flags: integer"
returns: "无"
---

**方法名称：** 设置 Dear ImGui 窗口标志。

**语法：** `imgui.setWindowFlags(handle, flags)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 窗口句柄。 |
| `flags` | `integer` | 是 | `imgui.WindowFlags` 中的单个值或多个值按位或后的结果。 |

| 返回值 | 说明 |
|---|---|
| 无 | 本方法没有返回值；未知位或内部标志会写入错误信息。 |

**详细说明：**

```lua
local flags = imgui.WindowFlags.NoResize | imgui.WindowFlags.NoCollapse
imgui.setWindowFlags(window, flags)
```

小鱼精灵始终附加 `NoSavedSettings`，窗口状态不会写入 Dear ImGui 的 ini 文件。
