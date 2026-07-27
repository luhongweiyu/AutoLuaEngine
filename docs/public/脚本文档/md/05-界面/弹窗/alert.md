---
params: "title: string, message: string, buttonText: string?"
returns: "boolean | nil, string?"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 提示框。

**语法：** `dialog.alert(title, message[, buttonText])`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `title` | `string` | 是 | 具体取值和组合规则见下方详细说明。 |
| `message` | `string` | 是 | 具体取值和组合规则见下方详细说明。 |
| `buttonText` | `string?` | 否 | 具体取值和组合规则见下方详细说明。 |

| 返回值 | 说明 |
|---|---|
| `boolean | nil, string?` | 具体字段、特殊值和失败情况见下方详细说明。 |

**详细说明：**

```lua
local ok, errorMessage = m.dialog.alert("提示", "操作完成", "知道了")
```

三个参数均为 `string`，`buttonText` 可省略。确认或关闭返回 `boolean`；创建失败返回
`nil, errorMessage:string`。

通用规则（悬浮窗、权限、options）见左侧「弹窗」。
