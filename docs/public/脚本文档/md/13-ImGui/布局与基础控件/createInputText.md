---
params: "parent: integer, label/value: string, inputType: integer?, width/height: number?"
returns: "integer"
---

**方法名称：** 创建文本输入框。

**语法：** `imgui.createInputText(parent, label[, value[, inputType[, width[, height]]]])`

**参数说明：**

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|---|---|---|---|---|
| `parent` | `integer` | 是 | - | 父容器句柄。 |
| `label` | `string` | 是 | - | 输入框标签和内部标识。 |
| `value` | `string` | 否 | `""` | 初始文本。 |
| `inputType` | `integer` | 否 | `0` | `Text`、`Password` 或 `Multiline`。 |
| `width` | `number` | 否 | `0` | `0` 自动，`-1` 填满可用宽度。 |
| `height` | `number` | 否 | `0` | 多行输入框高度；`0` 使用四行文本高度。 |

| 返回值 | 说明 |
|---|---|
| `integer` | 成功返回输入框句柄；失败返回 `0`。 |

**详细说明：**

```lua
local input = imgui.createInputText(
    parent, "名称", "小鱼精灵", imgui.InputType.Text, -1, 0
)
```

密码框只隐藏显示内容，`getInputText` 仍返回真实文本。多行模式支持换行和垂直滚动。
