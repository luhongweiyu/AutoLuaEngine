---
params: "sessionId: integer, data: any"
returns: "boolean | nil, string?"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 会话发消息。

**语法：** `ui.postMessage(sessionId, data)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `sessionId` | `integer` | 是 | 具体取值和组合规则见下方详细说明。 |
| `data` | `any` | 是 | 具体取值和组合规则见下方详细说明。 |

| 返回值 | 说明 |
|---|---|
| `boolean | nil, string?` | 具体字段、特殊值和失败情况见下方详细说明。 |

**使用示例：**

```lua
local result = ui.postMessage(0, nil)
print(result)
```

**详细说明：**

向会话发送消息。成功返回 `true`，失败返回 `nil, errorMessage:string`。
