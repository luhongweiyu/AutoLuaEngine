---
params: "surface: string, spec: table"
returns: "integer | nil, string?"
---

打开底层 UI 会话。`surface` 如 `"hud"` / `"web"` 等。成功返回 `sessionId`，失败返回
`nil, errorMessage:string`。

方法表见左侧「底层UI」。
