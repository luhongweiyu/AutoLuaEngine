---
params: "parent: integer, label: string, checked: boolean?, height: number?"
returns: "integer"
---

**方法名称：** 创建滑动开关控件。

**语法：** `imgui.createSwitch(parent, label[, checked[, height]])`

**参数说明：**

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|---|---|---|---|---|
| `parent` | `integer` | 是 | - | 父容器句柄。 |
| `label` | `string` | 是 | - | 开关右侧文字。 |
| `checked` | `boolean` | 否 | `false` | 初始状态。 |
| `height` | `number` | 否 | `0` | 开关高度；`0` 使用主题默认高度。 |

| 返回值 | 说明 |
|---|---|
| `integer` | 成功返回控件句柄；失败返回 `0`。 |

**详细说明：**

```lua
local control = imgui.createSwitch(parent, "保持运行", false, 42)
```

开关与复选框共用 `setChecked`、`isChecked` 和 `setOnCheck`。
