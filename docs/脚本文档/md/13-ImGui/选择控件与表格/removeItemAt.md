---
params: "handle: integer, index: integer"
returns: "无"
---

**方法名称：** 删除组合框或单选组的指定选项。

**语法：** `imgui.removeItemAt(handle, index)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 组合框或单选组句柄。 |
| `index` | `integer` | 是 | 从 `0` 开始的待删除索引。 |

| 返回值 | 说明 |
|---|---|
| 无 | 本方法没有返回值；索引越界时写入错误信息。 |

**详细说明：**

```lua
imgui.removeItemAt(combo, 1)
```

删除位于当前选择之前的项目会修正选择索引；删除当前项时选择相邻有效项，全部删除后为
`-1`。单选项对应的逐项换行信息会同时删除。
