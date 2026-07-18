---
params: "handle: integer, row/column: integer, text: string"
returns: "无"
---

**方法名称：** 设置表格单元格文本。

**语法：** `imgui.setTableItemText(handle, row, column, text)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 表格句柄。 |
| `row` / `column` | `integer` | 是 | 从 `0` 开始的行、列索引。 |
| `text` | `string` | 是 | 新文本。 |

| 返回值 | 说明 |
|---|---|
| 无 | 本方法没有返回值；行列越界时写入错误信息。 |

**详细说明：**

```lua
imgui.setTableItemText(tableView, row, 1, "已完成")
```

该方法不会自动插入行；必须先通过 `insertTableRow` 创建目标行。
