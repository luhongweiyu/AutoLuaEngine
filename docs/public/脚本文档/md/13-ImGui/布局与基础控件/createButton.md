---
params: "parent/text/w/h 或 x/y/w/h/text"
returns: "integer"
---

**方法名称：** 创建按钮控件。

**语法：** `imgui.createButton(parent, text, width, height)`

**语法：** `imgui.createButton(x, y, width, height, text)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `parent` | `integer` | 四参数形式 | 父容器句柄。 |
| `x` / `y` | `number` | 五参数形式 | 全屏画布上的绝对坐标。 |
| `text` | `string` | 是 | 按钮文字。 |
| `width` / `height` | `number` | 是 | `0` 自动计算，`-1` 使用父容器可用空间。 |

| 返回值 | 说明 |
|---|---|
| `integer` | 成功返回按钮句柄；失败返回 `0`。 |

**详细说明：**

```lua
local normal = imgui.createButton(parent, "运行", 180, 56)
local absolute = imgui.createButton(20, 40, 180, 56, "悬浮按钮")
```

四参数父容器形式是推荐写法；五参数形式用于兼容懒人精灵网页签名，按钮直接放在根画布。
