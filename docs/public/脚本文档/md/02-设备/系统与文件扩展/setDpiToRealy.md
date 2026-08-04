---
params: ""
returns: ""
---

**方法名称：** 恢复实际屏幕 DPI。

**语法：** `setDpiToRealy()`

**参数说明：** 无。

| 返回值 | 说明 |
|---|---|
| 无 | 恢复系统原始显示密度；失败时抛出 Lua 错误。 |

**使用示例：**

```lua
setDpiToRealy()
```

**详细说明：**

用于撤销 `setDpiToVir(dpi)` 对当前运行环境的显示密度设置。
