---
params: "Lua 5.4 标准参数"
returns: "遵循 Lua 5.4 标准库"
---

# Lua 5.4 标准库

脚本运行时直接提供 Lua 5.4 标准库。已有能力不再另造一层同义接口，常用的文件和时间操作可直接
使用 `io`、`os`、`string`、`table`、`math`、`utf8`、`coroutine` 等标准模块。

| 需求 | 推荐入口 |
|---|---|
| 读取或写入脚本工作目录中的文件 | `io.open`、`file:read`、`file:write` |
| 日期格式化、秒级时间或进程环境 | `os.date`、`os.time`、`os.getenv` |
| 毫秒级当前时间 | `systemTime()` |
| 统计脚本已运行时长 | `tickCount()` |

`io` 和 `os` 的参数、返回值和错误语义遵循 Lua 5.4。涉及应用权限、设备状态、触控或网络时，应使用
各自分类中的项目接口，而不是假设标准库能直接访问 Android 平台能力。

需要在受控条件下调用原生 C ABI 时，使用 [FFI（实验性）](FFI/概述.md)。
