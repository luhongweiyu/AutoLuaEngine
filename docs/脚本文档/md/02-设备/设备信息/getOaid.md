---
params: "无"
returns: "string?"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 获取OAID。

**语法：** `getOaid()`

**参数说明：** 无。

| 返回值 | 说明 |
|---|---|
| `string?` | 返回字符串；系统未提供该信息时可能为 nil。 |

**使用示例：**

```lua
local result = getOaid()
print(result)
```

**详细说明：**

当前版本尚未接入 OEM OAID 提供方，因此始终返回 `nil`。保留此函数名，后续接入后再
按 OEM 实际可用性返回 OAID 或 `nil`。
