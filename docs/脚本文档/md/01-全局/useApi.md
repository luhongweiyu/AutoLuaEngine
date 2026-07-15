---
params: "name: string"
returns: "boolean | nil, string?"
---

将指定命名空间的一层成员导出到 `_G`。

- 成功返回 `true`
- 失败返回 `nil, errorMessage`

`switchApi(name)` 与 `useApi(name)` 行为一致。

懒人精灵兼容示例：

```lua
m.useApi("lr")
beginThread(callback, ...)
local thread = Thread.newThread(callback, ...)
thread:stopThread()
```
