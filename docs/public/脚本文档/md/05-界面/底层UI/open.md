---
params: "surface: string, spec: table"
returns: "integer | nil, string?"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 打开底层会话。

**语法：** `ui.open(surface, spec)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `surface` | `string` | 是 | 具体取值和组合规则见下方详细说明。 |
| `spec` | `table` | 是 | 具体取值和组合规则见下方详细说明。 |

| 返回值 | 说明 |
|---|---|
| `integer | nil, string?` | 具体字段、特殊值和失败情况见下方详细说明。 |

**使用示例：**

```lua
local result = ui.open("示例", {})
print(result)
```

**详细说明：**

打开底层 UI 会话。`surface` 如 `"hud"` / `"web"` 等。成功返回 `sessionId`，失败返回
`nil, errorMessage:string`。

方法表见左侧「底层UI」。
