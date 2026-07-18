---
params: "parent: integer, label: string"
returns: "integer 或 nil"
---

**方法名称：** 创建单选按钮组。

**语法：** `imgui.createRadioGroup(parent, label)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `parent` | `integer` | 是 | 父容器句柄。 |
| `label` | `string` | 是 | 单选组上方显示的标签；空字符串不显示。 |

| 返回值 | 说明 |
|---|---|
| `integer` | 成功返回单选组句柄。 |
| `nil` | 创建失败。 |

**详细说明：**

```lua
local radio = assert(imgui.createRadioGroup(parent, "运行模式"))
imgui.addRadioBox(radio, "普通", false)
imgui.addRadioBox(radio, "高速", true)
```

新建单选组没有选中项，初始索引为 `-1`。
