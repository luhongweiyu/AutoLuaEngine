---
params: "id: any"
returns: "boolean | nil, string?"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 关闭 HUD。

**语法：** `hud.hide(id)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `id` | `any` | 是 | 由脚本定义的 HUD 逻辑标识。 |

| 返回值 | 说明 |
|---|---|
| `boolean | nil, string?` | 具体字段、特殊值和失败情况见下方详细说明。 |

**使用示例：**

```lua
local result = hud.hide(nil)
print(result)
```

**详细说明：**

关闭指定 `id` 的 HUD。成功关闭返回 `true`，不存在返回 `false`，失败返回
`nil, errorMessage:string`。
