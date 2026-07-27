---
params: "handle: integer, index: integer"
returns: "无"
---

**方法名称：** 设置组合框、单选组或表格的选择项。

**语法：** `imgui.setItemSelected(handle, index)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 组合框、单选组或表格句柄。 |
| `index` | `integer` | 是 | 从 `0` 开始的选项或表格行索引。 |

| 返回值 | 说明 |
|---|---|
| 无 | 本方法没有返回值；索引或控件无效时写入错误信息。 |

**详细说明：**

```lua
imgui.setItemSelected(combo, 1)
```

组合框和单选组会触发 `setOnSelectEvent`；表格会选择该行第 `0` 列，并触发
`setOnSelectEventEx`。重复设置当前选项不会重复触发回调。
