---
params: "无"
returns: "string?"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 获取IMSI。

**语法：** `getSubscriberId()`

**参数说明：** 无。

| 返回值 | 说明 |
|---|---|
| `string?` | 返回字符串；系统未提供该信息时可能为 nil。 |

**使用示例：**

```lua
local result = getSubscriberId()
print(result)
```

**详细说明：**

IMSI；系统未公开该数据时为 `nil`。
