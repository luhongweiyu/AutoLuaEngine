---
params: "title: string, x/y/w/h: number, showclose: boolean"
returns: "integer"
---

**方法名称：** 创建一个 Dear ImGui 内容窗口。

**语法：** `imgui.createWindow(title, x, y, width, height, showclose)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `title` | `string` | 是 | 窗口标题，支持 UTF-8。 |
| `x` / `y` | `number` | 是 | 相对当前 ImGui Surface 左上角的坐标。 |
| `width` / `height` | `number` | 是 | 窗口尺寸，必须大于 `0`。 |
| `showclose` | `boolean` | 是 | 是否显示窗口自身的关闭按钮。 |

| 返回值 | 说明 |
|---|---|
| `integer` | 成功返回大于 `0` 的窗口句柄；失败返回 `0`。 |

**详细说明：**

```lua
local window = imgui.createWindow("工具", 20, 80, 620, 720, true)
assert(window > 0, imgui.getLastError())
```

窗口句柄可作为布局和控件的父句柄。句柄只在当前脚本任务内有效。
