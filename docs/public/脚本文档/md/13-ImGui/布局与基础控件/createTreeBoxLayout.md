---
params: "parent: integer, title: string, width: number"
returns: "integer 或 nil"
---

**方法名称：** 创建默认展开的树形布局容器。

**语法：** `imgui.createTreeBoxLayout(parent, title, width)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `parent` | `integer` | 是 | 父容器句柄。 |
| `title` | `string` | 是 | 树节点标题。 |
| `width` | `number` | 是 | 宽度；`0` 自动计算，`-1` 使用可用宽度。 |

| 返回值 | 说明 |
|---|---|
| `integer` | 成功返回可作为父容器的树形布局句柄。 |
| `nil` | 创建失败。 |

**详细说明：**

```lua
local tree = assert(imgui.createTreeBoxLayout(parent, "高级设置", -1))
imgui.createCheckBox(tree, "记录日志", true)
```

`setLayoutBorderVisible(tree, true)` 会为完整树形容器显示边框。
