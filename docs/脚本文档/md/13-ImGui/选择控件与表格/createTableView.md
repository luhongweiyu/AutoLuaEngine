---
params: "parent: integer, title: string, columns: integer, showheader: boolean, width/height: number?"
returns: "integer 或 nil"
---

**方法名称：** 创建表格视图。

**语法：** `imgui.createTableView(parent, title, columns, showheader[, width[, height]])`

**参数说明：**

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|---|---|---|---|---|
| `parent` | `integer` | 是 | - | 父容器句柄。 |
| `title` | `string` | 是 | - | 表格内部标识，同一父容器内应唯一。 |
| `columns` | `integer` | 是 | - | 列数，必须大于 `0`。 |
| `showheader` | `boolean` | 是 | - | 是否显示表头行。 |
| `width` | `number` | 否 | `-1` | 表格宽度。 |
| `height` | `number` | 否 | `-1` | 表格高度。 |

| 返回值 | 说明 |
|---|---|
| `integer` | 成功返回表格句柄。 |
| `nil` | 参数或父句柄无效。 |

**详细说明：**

```lua
local tableView = assert(imgui.createTableView(parent, "状态", 2, true, -1, 240))
imgui.setTableHeaderItem(tableView, 0, "名称")
imgui.setTableHeaderItem(tableView, 1, "结果")
```

表格支持纵向滚动、行背景、边框和用户调整列宽。行列索引均从 `0` 开始。
