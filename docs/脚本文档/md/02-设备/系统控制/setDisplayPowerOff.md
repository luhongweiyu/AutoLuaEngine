---
params: "isPowerOff: boolean"
returns: "无"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 设置开关屏幕。

**语法：** `setDisplayPowerOff(isPowerOff)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `isPowerOff` | `boolean` | 是 | 具体取值和组合规则见下方详细说明。 |

| 返回值 | 说明 |
|---|---|
| 无 | 此方法不返回值。 |

**使用示例：**

```lua
setDisplayPowerOff(true)
```

**详细说明：**

`true` 息屏运行，`false` 恢复亮屏，需要 RootDaemon。
