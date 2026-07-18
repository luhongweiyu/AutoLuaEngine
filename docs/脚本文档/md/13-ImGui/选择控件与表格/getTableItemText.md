---
params: "handle: integer, row/column: integer"
returns: "string 或 nil"
---

**方法名称：** 获取表格单元格文本。

**语法：** `imgui.getTableItemText(handle, row, column)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 表格句柄。 |
| `row` / `column` | `integer` | 是 | 从 `0` 开始的行、列索引。 |

| 返回值 | 说明 |
|---|---|
| `string` | 单元格原始文本；空单元格返回空字符串。 |
| `nil` | 表格无效或行列越界。 |

**详细说明：**

```lua
local value = imgui.getTableItemText(tableView, 0, 1)
print(value or imgui.getLastError())
```

返回文本不包含 Dear ImGui 内部使用的隐藏 ID。
