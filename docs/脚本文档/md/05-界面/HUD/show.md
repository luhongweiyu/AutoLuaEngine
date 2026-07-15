---
params: "id: any, spec: table"
returns: "integer | nil, string?"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 显示 HUD。

**语法：** `hud.show(id, spec)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `id` | `any` | 是 | 由脚本定义的 HUD 逻辑标识。 |
| `spec` | `table` | 是 | 具体取值和组合规则见下方详细说明。 |

| 返回值 | 说明 |
|---|---|
| `integer | nil, string?` | 具体字段、特殊值和失败情况见下方详细说明。 |

**使用示例：**

```lua
local result = hud.show(nil, {})
print(result)
```

**详细说明：**

创建或替换指定 `id` 的 HUD。成功返回 `handle:integer`，失败返回
`nil, errorMessage:string`。同一 `id` 会替换旧 HUD。

字段与事件见左侧「HUD」。
