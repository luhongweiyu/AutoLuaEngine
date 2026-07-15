---
params: "title: string, items: table, selectedIndex: integer?, options: table?"
returns: "integer, string 或 nil, string?"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 单选框。

**语法：** `dialog.select(title, items[, selectedIndex[, options]])`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `title` | `string` | 是 | 具体取值和组合规则见下方详细说明。 |
| `items` | `table` | 是 | 具体取值和组合规则见下方详细说明。 |
| `selectedIndex` | `integer?` | 否 | 具体取值和组合规则见下方详细说明。 |
| `options` | `table?` | 否 | 具体取值和组合规则见下方详细说明。 |

| 返回值 | 说明 |
|---|---|
| `integer, string 或 nil, string?` | 具体字段、特殊值和失败情况见下方详细说明。 |

**详细说明：**

```lua
local index, value = m.dialog.select("选择模式", { "快", "稳", "调试" }, 2)
```

确认返回 `index:integer, value:string`，索引从 `1` 开始；取消返回 `nil`；创建失败返回
`nil, errorMessage:string`。

通用规则与 `options` 见左侧「弹窗」。
