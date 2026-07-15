---
params: "无"
returns: "width: integer, height: integer"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 获取屏幕宽高。

**语法：** `getDisplaySize()`

**参数说明：** 无。

| 返回值 | 说明 |
|---|---|
| `width: integer, height: integer` | 具体字段、特殊值和失败情况见下方详细说明。 |

**使用示例：**

```lua
local width, height = getDisplaySize()
print(width, height)
```

**详细说明：**

真实屏幕宽高。
