---
params: "无"
returns: "string?"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 获取当前前台 Activity 的完整类名。

**语法：** `getCurrentActivity()`

**参数说明：** 无。

| 返回值 | 说明 |
|---|---|
| `string?` | 返回字符串；系统未提供该信息时可能为 nil。 |

**使用示例：**

```lua
local result = getCurrentActivity()
print(result)
```

**详细说明：**

当前前台 Activity 的完整类名。
