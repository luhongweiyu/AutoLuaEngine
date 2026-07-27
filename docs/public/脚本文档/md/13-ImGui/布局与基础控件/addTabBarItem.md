---
params: "tabBarHandle: integer, title: string"
returns: "integer 或 nil"
---

**方法名称：** 向标签栏添加标签页。

**语法：** `imgui.addTabBarItem(tabBarHandle, title)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `tabBarHandle` | `integer` | 是 | `createTabBar` 返回的标签栏句柄。 |
| `title` | `string` | 是 | 标签页标题。 |

| 返回值 | 说明 |
|---|---|
| `integer` | 成功返回可作为父容器的标签页句柄。 |
| `nil` | 标签栏无效或创建失败。 |

**详细说明：**

```lua
local page = assert(imgui.addTabBarItem(tabBar, "日志"))
imgui.createLabel(page, "暂无日志", false)
```

懒人精灵网页把返回值写成无，但其 LSP 和实际嵌套控件用法需要标签页句柄；小鱼精灵返回
句柄，忽略返回值的旧脚本不受影响。
