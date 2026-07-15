---
params: "handle: integer, data: any"
returns: "boolean | nil, string?"
---

向页面发送 JSON 数据：调用页面的 `xiaoyv.onMessage(data)`，并派发
`xiaoyv-message` 事件。成功返回 `true`，失败返回 `nil, errorMessage:string`。
