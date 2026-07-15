---
params: "intent: table"
returns: "boolean"
---

打开 Android Intent，返回 `true` 表示系统已接受启动请求。`intent` 支持以下字段：

| 字段 | 类型 | 说明 |
|---|---|---|
| `action` | `string?` | Intent action；省略时使用 `android.intent.action.VIEW` |
| `uri` | `string?` | 目标 URI |
| `data` | `string?` | 目标 URI；同时传入时优先于 `uri` |
| `packageName` | `string?` | 限制 Intent 只由指定包处理 |
| `extra` | `table?` | 键值参数；字符串、布尔、整数和小数按原类型传递，复杂值转为 JSON 文本 |

`m.runIntent` 示例：

```lua
m.runIntent({
    action = "android.settings.SETTINGS",
})

m.runIntent({
    action = "android.intent.action.VIEW",
    data = "https://example.com",
    extra = { source = "xiaoyv" },
})
```
