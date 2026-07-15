---
params: "handle: integer, timeoutMs: integer?"
returns: "table | nil, string?"
---

等待页面事件。成功返回 `event:table`，失败返回 `nil, errorMessage:string`。
常见 `event.type`：`closed`、页面 `xiaoyv.emit` 的自定义类型等。

用法见左侧「HTML」示例。

省略 `timeoutMs` 表示一直等待。
