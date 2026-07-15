---
params: "name: string"
returns: "boolean 或 nil, string"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 切换全局 API 命名空间。

**语法：** `useApi(name)` / `switchApi(name)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `name` | `string` | 是 | 具体取值和组合规则见下方详细说明。 |

| 返回值 | 说明 |
|---|---|
| `boolean 或 nil, string` | 具体字段、特殊值和失败情况见下方详细说明。 |

**详细说明：**

小鱼精灵启动时已经默认启用 `m`，普通新脚本不需要调用本方法。

本方法用于兼容脚本：将指定命名空间的一层成员导出到 `_G`。支持的名称为：

- `m`、`mine`、`default`：小鱼精灵默认 API。
- `lr`、`lazy`：懒人精灵兼容 API。
- `cd`、`touchsprite`：触动精灵兼容 API。

- 成功返回 `true`。
- 失败返回 `nil, errorMessage`。

`switchApi(name)` 与 `useApi(name)` 行为一致。

建议在脚本开头、业务代码前调用。该函数不检查全局名称覆盖；脚本作者自行处理同名函数或变量。

懒人精灵兼容示例：

```lua
useApi("lr")
beginThread(callback, ...)
local thread = Thread.newThread(callback, ...)
thread:stopThread()
```
