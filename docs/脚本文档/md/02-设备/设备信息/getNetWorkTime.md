---
params: "无"
returns: "string"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 获取系统时间字符串。

**语法：** `getNetWorkTime()`

**参数说明：** 无。

| 返回值 | 说明 |
|---|---|
| `string` | 返回字符串；数据来源和特殊情况见下方详细说明。 |

**使用示例：**

```lua
local result = getNetWorkTime()
print(result)
```

**详细说明：**

系统当前时间，格式 `yyyy-MM-dd_HH-mm-ss`。
