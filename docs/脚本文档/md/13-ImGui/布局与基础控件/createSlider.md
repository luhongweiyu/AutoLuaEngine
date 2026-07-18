---
params: "parent: integer, label: string, min/max/initialPos: integer, width: number?"
returns: "integer 或 nil"
---

**方法名称：** 创建整数滑动条。

**语法：** `imgui.createSlider(parent, label, min, max, initialPos[, width])`

**参数说明：**

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|---|---|---|---|---|
| `parent` | `integer` | 是 | - | 父容器句柄。 |
| `label` | `string` | 是 | - | 滑动条标签。 |
| `min` / `max` | `integer` | 是 | - | 取值范围，必须满足 `min < max`。 |
| `initialPos` | `integer` | 是 | - | 初始值，必须位于闭区间 `[min, max]`。 |
| `width` | `number` | 否 | `-1` | 宽度；`-1` 填满当前可用宽度。 |

| 返回值 | 说明 |
|---|---|
| `integer` | 成功返回滑动条句柄。 |
| `nil` | 参数或父句柄无效。 |

**详细说明：**

```lua
local slider = assert(imgui.createSlider(parent, "音量", 0, 100, 50, -1))
```

滑动过程中产生的高频事件会按同一控件合并，核心状态始终保留最新值。
