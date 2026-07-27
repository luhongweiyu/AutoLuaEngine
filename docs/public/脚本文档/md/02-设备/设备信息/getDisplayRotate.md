---
params: "无"
returns: "integer"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 获取屏幕旋转。

**语法：** `getDisplayRotate()`

**参数说明：** 无。

| 返回值 | 说明 |
|---|---|
| `integer` | 返回整数；单位和特殊值见下方详细说明。 |

**使用示例：**

```lua
local result = getDisplayRotate()
print(result)
```

**详细说明：**

`0`、`1`、`2`、`3` 分别表示 0、90、180、270 度旋转状态。
