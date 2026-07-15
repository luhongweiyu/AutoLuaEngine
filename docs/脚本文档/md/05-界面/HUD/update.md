---
params: "id: any, patch: table"
returns: "boolean | nil, string?"
---

按 `id` 更新已有 HUD，`patch` 为要覆盖的字段子集。成功返回 `true`，失败返回
`nil, errorMessage:string`。

字段说明见左侧「HUD」。
