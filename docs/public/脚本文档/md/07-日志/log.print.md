---
params: "text: string"
returns: "boolean"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 输出日志。

**语法：** `log.print(text)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `text` | `string` | 是 | 要输出、显示或输入的文本。 |

| 返回值 | 说明 |
|---|---|
| `boolean` | 返回 true 或 false；各状态的具体含义见下方详细说明。 |

**使用示例：**

```lua
local result = log.print("示例文本")
print(result)
```

**详细说明：**

输出日志文本。

对应 C ABI：`engine_logPrint`。
