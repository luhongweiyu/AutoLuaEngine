---
params: "sessionId: integer, timeoutMs: integer?"
returns: "table | nil, string?"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 等待会话事件。

**语法：** `ui.waitEvent(sessionId[, timeoutMs])`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `sessionId` | `integer` | 是 | 具体取值和组合规则见下方详细说明。 |
| `timeoutMs` | `integer?` | 否 | 具体取值和组合规则见下方详细说明。 |

| 返回值 | 说明 |
|---|---|
| `table | nil, string?` | 具体字段、特殊值和失败情况见下方详细说明。 |

**使用示例：**

```lua
local result = ui.waitEvent(0)
print(result)
```

**详细说明：**

等待会话事件。成功返回 `event:table`，失败返回 `nil, errorMessage:string`。
