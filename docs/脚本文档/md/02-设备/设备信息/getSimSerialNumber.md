---
params: "无"
returns: "string?"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 获取SIM 序列号。

**语法：** `getSimSerialNumber()`

**参数说明：** 无。

| 返回值 | 说明 |
|---|---|
| `string?` | 返回字符串；系统未提供该信息时可能为 nil。 |

**使用示例：**

```lua
local result = getSimSerialNumber()
print(result)
```

**详细说明：**

SIM 序列号；系统未公开该数据时为 `nil`。
