---
params: "enabled: boolean"
returns: "无"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 设置飞行模式。

**语法：** `setAirplaneMode(enabled)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `enabled` | `boolean` | 是 | 是否启用该系统功能。 |

| 返回值 | 说明 |
|---|---|
| 无 | 此方法不返回值。 |

**使用示例：**

```lua
setAirplaneMode(true)
```

**详细说明：**

开关飞行模式，需要 RootDaemon。
