---
params: "ms: integer"
returns: "boolean"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 脚本延时。

**语法：** `sleep(ms)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `ms` | `integer` | 是 | 时长，单位为毫秒。 |

| 返回值 | 说明 |
|---|---|
| `boolean` | 返回 true 或 false；各状态的具体含义见下方详细说明。 |

**使用示例：**

```lua
local result = sleep(0)
print(result)
```

**详细说明：**

脚本延时，成功返回 `true`。

也可用 `m.sleep(ms)`，参数与返回值一致。

在多线程场景下，`sleep` 会在等待期间释放 VM Gate，返回前重新获取。
