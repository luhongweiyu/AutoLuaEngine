---
params: "无"
returns: "integer"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 获取CPU 架构。

**语法：** `getCpuArch()`

**参数说明：** 无。

| 返回值 | 说明 |
|---|---|
| `integer` | 返回整数；单位和特殊值见下方详细说明。 |

**使用示例：**

```lua
local result = getCpuArch()
print(result)
```

**详细说明：**

`0` 为 x86 系列；`1` 为非 x86 系列。当前 Android 目标设备通常会得到 ARM 系列的 `1`。
