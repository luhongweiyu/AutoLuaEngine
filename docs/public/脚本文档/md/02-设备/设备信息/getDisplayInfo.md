---
params: "无"
returns: "table"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 获取屏幕详情。

**语法：** `getDisplayInfo()`

**参数说明：** 无。

| 返回值 | 说明 |
|---|---|
| `table` | 返回 Lua 表；字段结构见下方详细说明。 |

**详细说明：**

屏幕详情，字段见下表。

`m.getDisplayInfo()` 返回：

```lua
{
    width = 720,
    height = 1280,
    dpi = 240,
    density = 1.5,
    scaledDensity = 1.5,
    xdpi = 240,
    ydpi = 240,
    rotate = 0,
}
```
