---
params: "无"
returns: "integer"
---

```lua
print(getRunEnvType())
```

当前返回：

- `getRunEnvType()` 无参数，返回类型为 `integer`。
- `0`：RootDaemon 已就绪。
- `1`：无障碍运行环境已就绪。
- `-1`：当前没有可用运行环境。


也可用 `m.getRunEnvType()`；两种写法语义一致。
