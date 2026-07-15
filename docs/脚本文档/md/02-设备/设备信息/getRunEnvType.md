---
params: "无"
returns: "integer"
---

`0` 为 Root，`1` 为无障碍，`-1` 为当前无可用运行环境。

```lua
print(m.getRunEnvType())
```

当前返回：

- `m.getRunEnvType()` 无参数，返回类型为 `integer`。
- `0`：RootDaemon 已就绪。
- `1`：无障碍运行环境已就绪。
- `-1`：当前没有可用运行环境。
