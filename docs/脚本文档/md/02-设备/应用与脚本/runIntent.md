---
params: "intent: table"
returns: "boolean"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 打开 Intent。

**语法：** `runIntent(intent)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `intent` | `table` | 是 | 具体取值和组合规则见下方详细说明。 |

| 返回值 | 说明 |
|---|---|
| `boolean` | 返回 true 或 false；各状态的具体含义见下方详细说明。 |

**详细说明：**

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
