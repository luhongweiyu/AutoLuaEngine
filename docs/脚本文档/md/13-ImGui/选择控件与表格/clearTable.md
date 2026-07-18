---
params: "handle: integer"
returns: "无"
---

**方法名称：** 清空表格的全部数据行。

**语法：** `imgui.clearTable(handle)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 表格句柄。 |

| 返回值 | 说明 |
|---|---|
| 无 | 本方法没有返回值；句柄无效时写入错误信息。 |

**详细说明：**

```lua
imgui.clearTable(tableView)
assert(imgui.getItemCount(tableView) == 0)
```

列数、表头文字和是否显示表头保持不变，选择状态会恢复为未选择。
