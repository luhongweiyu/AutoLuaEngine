---
params: "无"
returns: "integer"
---

```lua
local tm = systemTime()
print(tm)

local tick = tickCount()
print(tick)
```

说明：

- `systemTime()` 无参数，返回 `integer` 类型的 Unix epoch 毫秒时间戳。
- `tickCount()` 无参数，返回 `integer` 类型的当前脚本运行毫秒数。
- 两个函数也可以通过 `m.systemTime()`、`m.tickCount()` 调用。


`tickCount()` 无参数，返回 `integer` 类型的当前脚本运行毫秒数。也可通过 `m.tickCount()` 调用。
