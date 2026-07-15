---
params: "title: string, message: string, positiveText: string?, negativeText: string?"
returns: "boolean | nil, string?"
---

```lua
local accepted, errorMessage = m.dialog.confirm("确认", "是否继续？", "继续", "取消")
```

四个参数均为 `string`，后两个可省略。确认返回 `true`，取消或关闭返回 `false`，创建失败
返回 `nil, errorMessage:string`。

通用规则见左侧「弹窗」。
