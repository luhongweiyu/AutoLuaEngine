---
params: "handle: integer, text: string"
returns: "无"
---

**方法名称：** 替换输入框文本。

**语法：** `imgui.setInputText(handle, text)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 输入框句柄。 |
| `text` | `string` | 是 | 新的 UTF-8 文本。 |

| 返回值 | 说明 |
|---|---|
| 无 | 本方法没有返回值；失败原因通过 `imgui.getLastError()` 获取。 |

**详细说明：**

```lua
imgui.setInputText(input, "新的内容")
```

修改会在下一帧显示，不会创建额外 Android 原生输入控件。
