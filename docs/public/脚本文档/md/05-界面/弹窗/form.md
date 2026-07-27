---
params: "spec: table"
returns: "table? 或 nil, string"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 多字段表单。

**语法：** `ui.form(spec)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `spec` | `table` | 是 | 具体取值和组合规则见下方详细说明。 |

| 返回值 | 说明 |
|---|---|
| `table? 或 nil, string` | 具体字段、特殊值和失败情况见下方详细说明。 |

**详细说明：**

```lua
local values, errorMessage = m.ui.form({
    title = "登录参数",
    message = "设置完成后确认",
    positiveText = "开始",
    negativeText = "取消",
    fields = {
        { name = "account", label = "账号", type = "text", hint = "账号" },
        { name = "password", label = "密码", type = "password" },
        { name = "count", label = "次数", type = "number", default = "1" },
        { name = "notice", label = "说明文字", type = "label" },
        { name = "enabled", label = "启用", type = "boolean", default = true },
        {
            name = "mode",
            label = "模式",
            type = "select",
            items = { "快", "稳" },
            selectedIndex = 1,
        },
    },
})
```

确认后 `values` 以 `name` 为键。例如 `values.account` 是字符串，`values.enabled` 是
布尔值，`values.mode` 是选中的文字。取消返回 `nil`，创建失败返回
`nil, errorMessage:string`。

字段：`name`、`label`、`type`、`hint` 为 `string`，`items` 为 `table<string>`，
`selectedIndex` 为 `integer`；`default` 按类型为 `string` 或 `boolean`。
`type` 支持见左侧「弹窗」。

每个字段的 `name`、`label`、`type`、`hint` 是 `string`，`items` 是 `table<string>`，`selectedIndex` 是 `integer`；`default` 按字段类型使用 `string` 或 `boolean`。
