---
params: "parent: integer, text: string, singleline: boolean?"
returns: "integer"
---

**方法名称：** 创建文本标签。

**语法：** `imgui.createLabel(parent, text[, singleline])`

**参数说明：**

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|---|---|---|---|---|
| `parent` | `integer` | 是 | - | 父容器句柄。 |
| `text` | `string` | 是 | - | UTF-8 纯文本。 |
| `singleline` | `boolean` | 否 | `true` | `false` 时按控件宽度自动换行。 |

| 返回值 | 说明 |
|---|---|
| `integer` | 成功返回标签句柄；失败返回 `0`。 |

**详细说明：**

```lua
local title = imgui.createLabel(parent, "脚本状态：运行中", true)
local detail = imgui.createLabel(parent, "这是一段可以自动换行的详细内容。", false)
```

文本是普通 Dear ImGui 文本，不解析 HTML。多行标签可用 `setWidgetSize` 设置换行宽度。
