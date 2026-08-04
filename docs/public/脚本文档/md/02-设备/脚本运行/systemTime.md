---
params: "无"
returns: "integer"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 系统时间戳。

**语法：** `systemTime()`

**参数说明：** 无。

| 返回值 | 说明 |
|---|---|
| `integer` | 返回整数；单位和特殊值见下方详细说明。 |

**使用示例：**

```lua
local tm = systemTime()
print(tm)
```

**详细说明：**

`systemTime()` 无参数，返回 `integer` 类型的 Unix epoch 毫秒时间戳。
也可通过 `m.systemTime()` 调用。
