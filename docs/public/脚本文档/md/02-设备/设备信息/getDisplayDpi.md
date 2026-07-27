---
params: "无"
returns: "integer"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 获取屏幕 DPI。

**语法：** `getDisplayDpi()`

**参数说明：** 无。

| 返回值 | 说明 |
|---|---|
| `integer` | 返回整数；单位和特殊值见下方详细说明。 |

**使用示例：**

```lua
local result = getDisplayDpi()
print(result)
```

**详细说明：**

真实显示 DPI。
