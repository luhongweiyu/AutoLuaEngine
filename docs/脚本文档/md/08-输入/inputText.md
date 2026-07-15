---
params: "text: string"
returns: "boolean"
---

模拟输入文字，仅 Root 模式。

```lua
local ok = inputText("hello nice!!!")
print(ok)
```

`inputText(text:string)` 返回 `boolean`。它通过 Root 注入按键事件实现，适合英文、数字和
常见符号。需要完整
Unicode 文本（包括中文、Emoji 和组合文本）时使用下方的 `imeLib`。
