---
params: "handle: integer, column: integer, text: string"
returns: "无"
---

**方法名称：** 设置表格指定列的表头文本。

**语法：** `imgui.setTableHeaderItem(handle, column, text)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 表格句柄。 |
| `column` | `integer` | 是 | 从 `0` 开始的列索引。 |
| `text` | `string` | 是 | 表头文本。 |

| 返回值 | 说明 |
|---|---|
| 无 | 本方法没有返回值；列越界时写入错误信息。 |

**详细说明：**

```lua
imgui.setTableHeaderItem(tableView, 0, "项目")
```

即使创建表格时 `showheader=false`，文本也会保存在模型中，只是不渲染表头行。
