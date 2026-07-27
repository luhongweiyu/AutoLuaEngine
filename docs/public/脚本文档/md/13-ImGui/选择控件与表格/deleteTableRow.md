---
params: "handle: integer, row: integer"
returns: "无"
---

**方法名称：** 删除表格指定行。

**语法：** `imgui.deleteTableRow(handle, row)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 表格句柄。 |
| `row` | `integer` | 是 | 从 `0` 开始的待删除行索引。 |

| 返回值 | 说明 |
|---|---|
| 无 | 本方法没有返回值；行越界时写入错误信息。 |

**详细说明：**

```lua
imgui.deleteTableRow(tableView, 0)
```

删除当前选中行会取消选择；删除其之前的行会自动修正选择索引。
