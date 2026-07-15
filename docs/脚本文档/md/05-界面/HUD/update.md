---
params: "id: any, patch: table"
returns: "boolean | nil, string?"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 更新 HUD。

**语法：** `hud.update(id, patch)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `id` | `any` | 是 | 由脚本定义的 HUD 逻辑标识。 |
| `patch` | `table` | 是 | 具体取值和组合规则见下方详细说明。 |

| 返回值 | 说明 |
|---|---|
| `boolean | nil, string?` | 具体字段、特殊值和失败情况见下方详细说明。 |

**使用示例：**

```lua
local result = hud.update(nil, {})
print(result)
```

**详细说明：**

按 `id` 更新已有 HUD，`patch` 为要覆盖的字段子集。成功返回 `true`，失败返回
`nil, errorMessage:string`。

字段说明见左侧「HUD」。
