---
params: "handle: integer"
returns: "无"
---

**方法名称：** 清空组合框或单选组的全部选项。

**语法：** `imgui.removeAllItems(handle)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 组合框或单选组句柄。 |

| 返回值 | 说明 |
|---|---|
| 无 | 本方法没有返回值；失败原因通过 `imgui.getLastError()` 获取。 |

**详细说明：**

```lua
imgui.removeAllItems(combo)
assert(imgui.getSelectedItemIndex(combo) == -1)
```

清空后项目数为 `0`，选择索引为 `-1`。
