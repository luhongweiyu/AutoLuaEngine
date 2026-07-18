---
params: "handle: integer, after: integer"
returns: "integer 或 nil"
---

**方法名称：** 向表格插入一行空数据。

**语法：** `imgui.insertTableRow(handle, after)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 表格句柄。 |
| `after` | `integer` | 是 | `-1` 插到开头，`-2` 追加末尾，非负数表示插到该行之后。 |

| 返回值 | 说明 |
|---|---|
| `integer` | 成功返回新行的从 `0` 开始索引。 |
| `nil` | 表格或插入位置无效。 |

**详细说明：**

```lua
local row = assert(imgui.insertTableRow(tableView, -2))
imgui.setTableItemText(tableView, row, 0, "网络")
imgui.setTableItemText(tableView, row, 1, "正常")
```

新行按创建表格时的列数初始化，所有单元格初始为空字符串。
