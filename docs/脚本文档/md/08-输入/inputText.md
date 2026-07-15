---
params: "text: string"
returns: "boolean"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 注入文本。

**语法：** `inputText(text)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `text` | `string` | 是 | 要输出、显示或输入的文本。 |

| 返回值 | 说明 |
|---|---|
| `boolean` | 返回 true 或 false；各状态的具体含义见下方详细说明。 |

**详细说明：**

模拟输入文字，仅 Root 模式。

```lua
local ok = inputText("hello nice!!!")
print(ok)
```

`inputText(text:string)` 返回 `boolean`。它通过 Root 注入按键事件实现，适合英文、数字和
常见符号。需要完整
Unicode 文本（包括中文、Emoji 和组合文本）时使用下方的 `imeLib`。
