---
params: "id: any, timeoutMs: integer?"
returns: "table | nil, string?"
---

等待指定 HUD 的事件。成功返回 `event:table`，失败返回 `nil, errorMessage:string`。
`event.type` 可为 `click`、`closed`、`error`、`timeout`。

超时语义与事件字段见左侧「HUD」。
