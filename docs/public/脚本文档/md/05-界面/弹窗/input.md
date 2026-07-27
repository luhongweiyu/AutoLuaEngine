---
params: "title: string, hint: string, defaultText: string?, options: table?"
returns: "string? 或 nil, string"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 输入框。

**语法：** `dialog.input(title, hint[, defaultText[, options]])`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `title` | `string` | 是 | 具体取值和组合规则见下方详细说明。 |
| `hint` | `string` | 是 | 具体取值和组合规则见下方详细说明。 |
| `defaultText` | `string?` | 否 | 具体取值和组合规则见下方详细说明。 |
| `options` | `table?` | 否 | 具体取值和组合规则见下方详细说明。 |

| 返回值 | 说明 |
|---|---|
| `string? 或 nil, string` | 具体字段、特殊值和失败情况见下方详细说明。 |

**详细说明：**

```lua
local text, errorMessage = m.dialog.input("输入", "请输入名称", "默认名称", {
    message = "该值会传给脚本",
    multiline = false,
    inputType = "text", -- text、number、password
    selectAll = true,
    positiveText = "保存",
    negativeText = "取消",
})
```

确认返回 `text:string`；取消返回 `nil`；创建失败返回 `nil, errorMessage:string`。
`options` 字段见左侧「弹窗」。
