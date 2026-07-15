---
params: "cmd: string, isRet: boolean?"
returns: "string? 或无"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 通过 RootDaemon 执行 Shell 命令。

**语法：** `exec(cmd[, isRet])`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `cmd` | `string` | 是 | 要执行的 Shell 命令文本。 |
| `isRet` | `boolean?` | 否 | 具体取值和组合规则见下方详细说明。 |

| 返回值 | 说明 |
|---|---|
| `string? 或无` | 具体字段、特殊值和失败情况见下方详细说明。 |

**使用示例：**

```lua
local output = exec("id")
print(output)
```

**详细说明：**

以 RootDaemon 权限执行 shell；`isRet` 默认 `true`，返回合并输出；为 `false` 时不返回结果。命令退出码由脚本根据输出自行判断。
