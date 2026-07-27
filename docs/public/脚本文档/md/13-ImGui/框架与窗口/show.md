---
params: "touchable: boolean?, font: string?, fontsize: number?"
returns: "boolean"
---

**方法名称：** 在全屏透明画布上显示 ImGui 框架。

**语法：** `imgui.show([touchable][, font][, fontsize])`

**参数说明：**

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|---|---|---|---|---|
| `touchable` | `boolean` | 否 | `true` | 是否接收触摸。为 `false` 时触摸会穿透到下层应用。 |
| `font` | `string` | 否 | 系统字体 | 字体文件路径；加载失败时依次尝试 Android 系统字体。 |
| `fontsize` | `number` | 否 | `45` | 内容字体大小，单位为物理像素，必须大于 `0`。 |

| 返回值 | 说明 |
|---|---|
| `boolean` | 显示并正常关闭返回 `true`；创建 Surface 或渲染器失败返回 `false`。 |

**详细说明：**

```lua
local window = imgui.createWindow("面板", 20, 80, 600, 700, true)
imgui.createLabel(window, "脚本运行中", true)

assert(imgui.show(true, 30), imgui.getLastError())
```

三个可选参数按类型识别，顺序可以交换，`nil` 会被忽略；同一种类型出现两次会产生 Lua
参数错误。`touchable=true` 时当前任务会阻塞到 `imgui.close()` 或外部关闭；为 `false` 时
显示成功后立即返回。回调由内部事件泵执行，仍可安全调用其他 `imgui` 方法。
