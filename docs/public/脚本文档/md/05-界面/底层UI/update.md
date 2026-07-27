---
params: "sessionId: integer, spec: table"
returns: "boolean | nil, string?"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 更新会话。

**语法：** `ui.update(sessionId, spec)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `sessionId` | `integer` | 是 | 具体取值和组合规则见下方详细说明。 |
| `spec` | `table` | 是 | 具体取值和组合规则见下方详细说明。 |

| 返回值 | 说明 |
|---|---|
| `boolean | nil, string?` | 具体字段、特殊值和失败情况见下方详细说明。 |

**使用示例：**

```lua
local result = ui.update(0, {})
print(result)
```

**详细说明：**

更新会话内容。成功返回 `true`，失败返回 `nil, errorMessage:string`。
