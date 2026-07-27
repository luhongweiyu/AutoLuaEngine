---
params: "config: table"
returns: "boolean"
---

**方法名称：** 创建并显示指定位置和尺寸的独立 ImGui 悬浮窗口。

**语法：** `imgui.showWindow(config)`

**参数说明：**

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|---|---|---|---|---|
| `x` / `y` | `integer` | 否 | `0` | 外层悬浮 Surface 的屏幕坐标。 |
| `width` / `height` | `integer` | 否 | `600` | 外层 Surface 尺寸，必须大于 `0`。 |
| `hastitle` | `boolean` | 否 | `true` | 是否显示外层可拖动标题栏。 |
| `title` | `string` | 否 | `""` | 外层标题文字。 |
| `titlecolor` | `integer` | 否 | `0xFFFFFFFF` | 标题文字颜色，格式为 `0xAARRGGBB`。 |
| `titlebgcolor` | `integer` | 否 | `0xFF87CEFA` | 标题栏背景颜色。 |
| `hasclose` | `boolean` | 否 | `true` | 是否显示关闭按钮。 |
| `closecolor` | `integer` | 否 | `0xFFFFFFFF` | 关闭按钮颜色。 |
| `hasresize` | `boolean` | 否 | `true` | 是否显示右下角缩放柄。 |
| `resizecolor` | `integer` | 否 | `0xFFFFFFFF` | 缩放柄颜色。 |
| `hastoggle` | `boolean` | 否 | `true` | 是否显示收起/展开按钮。 |
| `togglecolor` | `integer` | 否 | `0xFFFFFFFF` | 收起/展开按钮颜色。 |
| `fontsize` | `number` | 否 | `100` | 外层标题字体大小。 |
| `font` | `string` | 否 | 系统字体 | 内容字体路径，小鱼精灵扩展字段。 |
| `contentfontsize` | `number` | 否 | `45` | 内容字体大小，小鱼精灵扩展字段。 |

| 返回值 | 说明 |
|---|---|
| `boolean` | Surface 创建命令提交成功返回 `true`，失败返回 `false`。 |

**详细说明：**

```lua
local window = imgui.createWindow("状态", 10, 10, 300, 300, false)
imgui.createLabel(window, "正在运行", true)

assert(imgui.showWindow({
    x = 80,
    y = 160,
    width = 560,
    height = 760,
    title = "运行面板",
    fontsize = 34,
    contentfontsize = 28,
}), imgui.getLastError())

while true do sleep(1000) end
```

本方法非阻塞。显示时，第一个已创建的 ImGui 窗口会默认调整为 `(0, 0, width, height)`
填满外层画布，之后仍可调用 `setWindowPos`、`setWindowSize` 修改。主脚本结束会统一关闭
窗口，因此非阻塞脚本必须继续运行。
