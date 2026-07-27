---
params: "handle: integer"
returns: "boolean 或 nil"
---

**方法名称：** 获取复选框或开关的当前状态。

**语法：** `imgui.isChecked(handle)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 复选框或开关句柄。 |

| 返回值 | 说明 |
|---|---|
| `boolean` | `true` 表示选中或开启，`false` 表示未选中或关闭。 |
| `nil` | 句柄无效或控件类型不匹配。 |

**详细说明：**

```lua
if imgui.isChecked(enabled) then
    print("功能已启用")
end
```

状态在用户操作提交到核心模型后立即更新。
