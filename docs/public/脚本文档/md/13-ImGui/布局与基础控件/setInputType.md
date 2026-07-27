---
params: "handle: integer, inputType: integer"
returns: "无"
---

**方法名称：** 切换输入框类型。

**语法：** `imgui.setInputType(handle, inputType)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 输入框句柄。 |
| `inputType` | `integer` | 是 | `0` 普通、`1` 密码、`2` 多行；推荐使用 `imgui.InputType`。 |

| 返回值 | 说明 |
|---|---|
| 无 | 本方法没有返回值；类型值无效时写入错误信息。 |

**详细说明：**

```lua
imgui.setInputType(input, imgui.InputType.Password)
imgui.setInputText(input, "重新输入的密码")
```

类型真正发生变化时会清空原文本，与懒人精灵行为保持一致；重复设置相同类型不会清空。
