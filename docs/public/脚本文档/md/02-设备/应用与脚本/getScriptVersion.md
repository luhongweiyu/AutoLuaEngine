---
params: "无"
returns: "integer"
---

**方法名称：** 获取当前脚本版本号。

**语法：** `getScriptVersion()`

**参数说明：** 无。

| 返回值 | 说明 |
|---|---|
| `integer` | 当前脚本工作目录中 `version` 文件记录的整数版本号。 |

**详细说明：**

此兼容接口沿用懒人精灵脚本目录约定，读取 `getWorkPath() .. "/version"`。文件不存在、
无法读取或内容不是整数时抛出 Lua 错误，不会用 `0` 伪装成功。`.alpkg` 当前没有独立的
脚本版本字段；如需调用本接口，应把 `version` 文件放在脚本工作目录中。

```lua
local version = getScriptVersion()
print("当前脚本版本：" .. version)
```
