---
params: "title: string, hint: string, defaultText: string?, options: table?"
returns: "string? 或 nil, string"
---

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
