---
params: "title: string, message: string, positiveText: string?, negativeText: string?"
returns: "boolean | nil, string?"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 确认框。

**语法：** `dialog.confirm(title, message[, positiveText[, negativeText]])`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `title` | `string` | 是 | 具体取值和组合规则见下方详细说明。 |
| `message` | `string` | 是 | 具体取值和组合规则见下方详细说明。 |
| `positiveText` | `string?` | 否 | 具体取值和组合规则见下方详细说明。 |
| `negativeText` | `string?` | 否 | 具体取值和组合规则见下方详细说明。 |

| 返回值 | 说明 |
|---|---|
| `boolean | nil, string?` | 具体字段、特殊值和失败情况见下方详细说明。 |

**详细说明：**

```lua
local accepted, errorMessage = m.dialog.confirm("确认", "是否继续？", "继续", "取消")
```

四个参数均为 `string`，后两个可省略。确认返回 `true`，取消或关闭返回 `false`，创建失败
返回 `nil, errorMessage:string`。

通用规则见左侧「弹窗」。
