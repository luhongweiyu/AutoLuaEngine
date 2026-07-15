---
params: "无"
returns: "table"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 获取传感器列表。

**语法：** `getSensorsInfo()`

**参数说明：** 无。

| 返回值 | 说明 |
|---|---|
| `table` | 返回 Lua 表；字段结构见下方详细说明。 |

**使用示例：**

```lua
local result = getSensorsInfo()
print(result)
```

**详细说明：**

传感器详情数组。

`m.getSensorsInfo()` 的每项包含：`name`、`vendor`、`type`、`typeName`、`version`、
`maximumRange`、`resolution`、`power`、`minDelay`。
