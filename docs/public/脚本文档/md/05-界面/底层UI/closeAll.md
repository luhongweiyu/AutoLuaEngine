---
params: "无"
returns: "boolean"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 关闭全部会话。

**语法：** `ui.closeAll()`

**参数说明：** 无。

| 返回值 | 说明 |
|---|---|
| `boolean` | 返回 true 或 false；各状态的具体含义见下方详细说明。 |

**使用示例：**

```lua
local result = ui.closeAll()
print(result)
```

**详细说明：**

关闭当前脚本创建的全部 UI 会话。返回 `boolean`。
