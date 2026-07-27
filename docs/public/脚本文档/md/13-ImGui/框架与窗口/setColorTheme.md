---
params: "style: integer"
returns: "boolean"
---

**方法名称：** 设置 ImGui 全局颜色主题。

**语法：** `imgui.setColorTheme(style)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `style` | `integer` | 是 | `1` 为浅色，`2` 为经典，其他值为暗色；推荐使用 `imgui.Theme` 常量。 |

| 返回值 | 说明 |
|---|---|
| `boolean` | 当前环境支持 ImGui 且设置成功返回 `true`，不支持时返回 `false`。 |

**详细说明：**

```lua
assert(imgui.setColorTheme(imgui.Theme.Dark), imgui.getLastError())
```

设置会保留到当前脚本任务结束；已经显示的界面会在下一帧更新主题。
