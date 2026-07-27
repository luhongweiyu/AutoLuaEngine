---
params: "handle: integer, callback: function|nil"
returns: "无"
---

**方法名称：** 设置或移除滑动条变化回调。

**语法：** `imgui.setOnSliderEvent(handle, callback)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 滑动条句柄。 |
| `callback` | `function` 或 `nil` | 是 | `function(handle, position)`；传 `nil` 移除。 |

| 返回值 | 说明 |
|---|---|
| 无 | 本方法没有返回值。 |

**详细说明：**

```lua
imgui.setOnSliderEvent(slider, function(handle, position)
    imgui.setProgressBarPos(progress, position / 100)
end)
```

回调参数 `position` 为整数。未消费的连续滑动事件只保留最新值，避免拖动时堆积队列。
