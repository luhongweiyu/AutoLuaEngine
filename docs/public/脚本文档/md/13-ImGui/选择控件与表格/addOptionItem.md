---
params: "handle: integer, itemText: string"
returns: "无"
---

**方法名称：** 向组合框追加选项。

**语法：** `imgui.addOptionItem(handle, itemText)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | `createComboBox` 返回的组合框句柄。 |
| `itemText` | `string` | 是 | 要追加的选项文本，按普通字符串保存。 |

| 返回值 | 说明 |
|---|---|
| 无 | 本方法没有返回值；失败原因通过 `imgui.getLastError()` 获取。 |

**详细说明：**

```lua
imgui.addOptionItem(combo, "新增选项")
print(imgui.getItemCount(combo))
```

追加不会改变当前选择。空组合框追加第一项后仍返回 `-1`，需要时调用
`setItemSelected(handle, 0)` 显式选择。
