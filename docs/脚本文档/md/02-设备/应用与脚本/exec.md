---
params: "cmd: string, isRet: boolean?"
returns: "string? 或无"
---

以 RootDaemon 权限执行 shell；`isRet` 默认 `true`，返回合并输出；为 `false` 时不返回结果。命令退出码由脚本根据输出自行判断。
