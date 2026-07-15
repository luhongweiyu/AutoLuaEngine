---
params: "title: string, message: string, buttonText: string?"
returns: "boolean | nil, string?"
---

```lua
local ok, errorMessage = m.dialog.alert("提示", "操作完成", "知道了")
```

三个参数均为 `string`，`buttonText` 可省略。确认或关闭返回 `boolean`；创建失败返回
`nil, errorMessage:string`。

通用规则（悬浮窗、权限、options）见左侧「弹窗」。
