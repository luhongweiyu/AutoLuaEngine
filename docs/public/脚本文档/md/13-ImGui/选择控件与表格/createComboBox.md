---
params: "parent: integer, items: string|table, width: number?"
returns: "integer"
---

**方法名称：** 创建组合框。

**语法：** `imgui.createComboBox(parent, items[, width])`

**参数说明：**

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|---|---|---|---|---|
| `parent` | `integer` | 是 | - | 父容器句柄。 |
| `items` | `string` 或 `table` | 是 | - | 竖线分隔字符串或从 `1` 开始的字符串数组。 |
| `width` | `number` | 否 | `0` | `0` 自动计算，`-1` 填满可用宽度。 |

| 返回值 | 说明 |
|---|---|
| `integer` | 成功返回组合框句柄；失败返回 `0`。 |

**详细说明：**

```lua
local combo1 = imgui.createComboBox(parent, "普通|快速|调试", -1)
local combo2 = imgui.createComboBox(parent, {"甲", "乙"}, 200)
```

字符串中的字面竖线写成 `\|`，字面反斜杠可写成 `\\`。初始项目非空时默认选择第 `0`
项；空数组创建的组合框保持 `-1` 未选择状态。
