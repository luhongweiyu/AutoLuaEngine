---
params: "title: string, items: table, selectedIndex: integer?, options: table?"
returns: "integer, string 或 nil, string?"
---

```lua
local index, value = m.dialog.select("选择模式", { "快", "稳", "调试" }, 2)
```

确认返回 `index:integer, value:string`，索引从 `1` 开始；取消返回 `nil`；创建失败返回
`nil, errorMessage:string`。

通用规则与 `options` 见左侧「弹窗」。
