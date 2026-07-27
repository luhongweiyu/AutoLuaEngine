---
params: "text: any, durationMs: integer?"
returns: "integer | nil, string?"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 自动关闭提示。

**语法：** `toast(text[, durationMs])`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `text` | `any` | 是 | 要输出、显示或输入的文本。 |
| `durationMs` | `integer?` | 否 | 具体取值和组合规则见下方详细说明。 |

| 返回值 | 说明 |
|---|---|
| `integer | nil, string?` | 具体字段、特殊值和失败情况见下方详细说明。 |

**使用示例：**

```lua
local result = toast("示例文本")
print(result)
```

**详细说明：**

显示自动关闭的 HUD 提示，不阻塞脚本。成功返回 `handle:integer`，失败返回
`nil, errorMessage:string`。

通用字段见左侧「HUD」。
