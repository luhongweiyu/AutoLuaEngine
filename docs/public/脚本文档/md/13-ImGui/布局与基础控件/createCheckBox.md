---
params: "parent: integer, label: string, checked: boolean?"
returns: "integer"
---

**方法名称：** 创建复选框。

**语法：** `imgui.createCheckBox(parent, label[, checked])`

**参数说明：**

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|---|---|---|---|---|
| `parent` | `integer` | 是 | - | 父容器句柄。 |
| `label` | `string` | 是 | - | 复选框旁显示的文字。 |
| `checked` | `boolean` | 否 | `false` | 初始选中状态。 |

| 返回值 | 说明 |
|---|---|
| `integer` | 成功返回控件句柄；失败返回 `0`。 |

**详细说明：**

```lua
local enabled = imgui.createCheckBox(parent, "启用识别", true)
assert(enabled > 0, imgui.getLastError())
```

使用 `isChecked` 读取状态，使用 `setChecked` 修改状态。
