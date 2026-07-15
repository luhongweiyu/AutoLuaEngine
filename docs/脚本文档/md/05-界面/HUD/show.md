---
params: "id: any, spec: table"
returns: "integer | nil, string?"
---

创建或替换指定 `id` 的 HUD。成功返回 `handle:integer`，失败返回
`nil, errorMessage:string`。同一 `id` 会替换旧 HUD。

字段与事件见左侧「HUD」。
