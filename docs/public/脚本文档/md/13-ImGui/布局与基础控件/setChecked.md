---
params: "handle: integer, checked: boolean"
returns: "无"
---

**方法名称：** 设置复选框或开关的选中状态。

**语法：** `imgui.setChecked(handle, checked)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 复选框或开关句柄。 |
| `checked` | `boolean` | 是 | 新状态。 |

| 返回值 | 说明 |
|---|---|
| 无 | 本方法没有返回值；类型不匹配时写入错误信息。 |

**详细说明：**

```lua
imgui.setChecked(enabled, true)
```

小鱼精灵会把程序设置产生的实际状态变化投递给 `setOnCheck` 回调，使脚本设置和用户点击
采用一致的事件路线；重复设置当前值不会重复触发回调。
