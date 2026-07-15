---
params: "enabled: boolean"
returns: "无"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 设置Wi-Fi 开关。

**语法：** `setWifiEnable(enabled)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `enabled` | `boolean` | 是 | 是否启用该系统功能。 |

| 返回值 | 说明 |
|---|---|
| 无 | 此方法不返回值。 |

**使用示例：**

```lua
setWifiEnable(true)
```

**详细说明：**

开关 Wi-Fi，需要 RootDaemon。
