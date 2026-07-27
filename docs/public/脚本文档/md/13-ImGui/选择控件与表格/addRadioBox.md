---
params: "handle: integer, text: string, wrapline: boolean?"
returns: "boolean"
---

**方法名称：** 向单选组追加一个单选项。

**语法：** `imgui.addRadioBox(handle, text[, wrapline])`

**参数说明：**

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|---|---|---|---|---|
| `handle` | `integer` | 是 | - | `createRadioGroup` 返回的句柄。 |
| `text` | `string` | 是 | - | 单选项显示文本。 |
| `wrapline` | `boolean` | 否 | `false` | `true` 表示本项之后的下一项另起一行。 |

| 返回值 | 说明 |
|---|---|
| `boolean` | 添加成功返回 `true`，句柄无效返回 `false`。 |

**详细说明：**

```lua
assert(imgui.addRadioBox(radio, "甲", false)) -- 下一项同行
assert(imgui.addRadioBox(radio, "乙", true))  -- 下一项换行
assert(imgui.addRadioBox(radio, "丙", false))
```

懒人精灵网页写成无返回值，1.7.6 LSP 写成布尔值；小鱼精灵返回布尔值，旧脚本忽略它不受
影响。`wrapline` 是逐项属性，不会覆盖已经添加项目的排列方式。
