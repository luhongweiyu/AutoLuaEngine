---
params: "无"
returns: "table"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 获取已装包名。

**语法：** `getInstalledApps()`

**参数说明：** 无。

| 返回值 | 说明 |
|---|---|
| `table` | 返回 Lua 表；字段结构见下方详细说明。 |

**使用示例：**

```lua
local result = getInstalledApps()
print(result)
```

**详细说明：**

已安装包名数组。
