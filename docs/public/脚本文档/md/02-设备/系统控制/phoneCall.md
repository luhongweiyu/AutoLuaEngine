---
params: "number: string, state: integer?"
returns: "无"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 拨打电话。

**语法：** `phoneCall(number[, state])`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `number` | `string` | 是 | 具体取值和组合规则见下方详细说明。 |
| `state` | `integer?` | 否 | 具体取值和组合规则见下方详细说明。 |

| 返回值 | 说明 |
|---|---|
| 无 | 此方法不返回值。 |

**使用示例：**

```lua
phoneCall("10086")
```

**详细说明：**

`state=0` 拨号，其他值挂断，需要 RootDaemon。
