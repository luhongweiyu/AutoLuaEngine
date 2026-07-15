---
params: "无"
returns: "integer"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 运行环境类型。

**语法：** `getRunEnvType()`

**参数说明：** 无。

| 返回值 | 说明 |
|---|---|
| `integer` | 返回整数；单位和特殊值见下方详细说明。 |

**详细说明：**

```lua
print(getRunEnvType())
```

当前返回：

- `getRunEnvType()` 无参数，返回类型为 `integer`。
- `0`：RootDaemon 已就绪。
- `1`：无障碍运行环境已就绪。
- `-1`：当前没有可用运行环境。


也可用 `m.getRunEnvType()`；两种写法语义一致。
