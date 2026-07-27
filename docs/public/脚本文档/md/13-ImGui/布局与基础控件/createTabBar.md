---
params: "parent: integer, title: string"
returns: "integer 或 nil"
---

**方法名称：** 创建标签栏容器。

**语法：** `imgui.createTabBar(parent, title)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `parent` | `integer` | 是 | 父容器句柄。 |
| `title` | `string` | 是 | 标签栏内部标识，同一父容器内应保持唯一。 |

| 返回值 | 说明 |
|---|---|
| `integer` | 成功返回标签栏句柄。 |
| `nil` | 创建失败。 |

**详细说明：**

```lua
local tabs = assert(imgui.createTabBar(parent, "主标签栏"))
local first = assert(imgui.addTabBarItem(tabs, "常规"))
imgui.createLabel(first, "常规内容", true)
```

标签栏本身不能直接容纳普通控件，控件应添加到 `addTabBarItem` 返回的标签页句柄中。
