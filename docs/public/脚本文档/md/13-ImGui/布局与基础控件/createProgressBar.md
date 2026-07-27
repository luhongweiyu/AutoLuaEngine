---
params: "parent: integer, progress: number, width/height: number?"
returns: "integer"
---

**方法名称：** 创建水平进度条。

**语法：** `imgui.createProgressBar(parent, progress[, width[, height]])`

**参数说明：**

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|---|---|---|---|---|
| `parent` | `integer` | 是 | - | 父容器句柄。 |
| `progress` | `number` | 是 | - | 初始进度，超出 `0.0..1.0` 时自动限制。 |
| `width` | `number` | 否 | `0` | `0` 使用默认宽度，`-1` 填满。 |
| `height` | `number` | 否 | `0` | `0` 使用主题默认高度。 |

| 返回值 | 说明 |
|---|---|
| `integer` | 成功返回进度条句柄；失败返回 `0`。 |

**详细说明：**

```lua
local progress = imgui.createProgressBar(parent, 0.35, -1, 30)
```

进度条只显示状态，不接收用户输入。
