---
params: "handle: integer"
returns: "string 或 nil"
---

**方法名称：** 获取输入框当前文本。

**语法：** `imgui.getInputText(handle)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 输入框句柄。 |

| 返回值 | 说明 |
|---|---|
| `string` | 当前真实文本；空输入框返回空字符串。 |
| `nil` | 句柄无效或不是输入框。 |

**详细说明：**

```lua
local value = imgui.getInputText(input)
print(value or "读取失败")
```

密码输入框返回未掩码的原始内容，多行输入框保留换行符。
