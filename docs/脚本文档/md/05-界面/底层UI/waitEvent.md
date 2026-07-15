---
params: "sessionId: integer, timeoutMs: integer?"
returns: "table | nil, string?"
---

等待会话事件。成功返回 `event:table`，失败返回 `nil, errorMessage:string`。
