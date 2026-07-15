---
params: "spec: table"
returns: "integer | nil, string?"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 打开 HTML 页。

**语法：** `web.open(spec)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `spec` | `table` | 是 | 具体取值和组合规则见下方详细说明。 |

| 返回值 | 说明 |
|---|---|
| `integer | nil, string?` | 具体字段、特殊值和失败情况见下方详细说明。 |

**使用示例：**

```lua
local result = web.open({})
print(result)
```

**详细说明：**

打开 WebView/HTML 界面。成功返回 `handle:integer`，失败返回
`nil, errorMessage:string`。

`spec` 字段、来源优先级与布局规则见左侧「HTML」。
