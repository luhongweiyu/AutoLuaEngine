---
params: "relativePath: string"
returns: "string?"
---

**方法名称：** 取得已导入扩展的私有路径。

**语法：** `LuaEngine.getExtensionPath(relativePath)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `relativePath` | `string` | 是 | 以扩展页导入目录为根的相对路径，例如 `mem/arm64-v8a/libmem.so`。 |

| 返回值 | 说明 |
|---|---|
| `string` | 已导入文件或目录的私有绝对路径。 |
| `nil` | 路径不存在、未导入或不是安全相对路径。 |

**详细说明：**

扩展页只负责把文件或目录复制到应用私有目录。本方法只返回该副本路径，不加载文件、不判断 ABI，
也不推断 SO 依赖关系。需要原生扩展时由脚本自行按正确顺序调用 `ffi.load`。

```lua
import("com.nx.assist.lua.LuaEngine")

local ffi = require("ffi")
local runtime = assert(LuaEngine.getExtensionPath("myext/libc++_shared.so"))
local library = assert(LuaEngine.getExtensionPath("myext/arm64-v8a/libexample.so"))

ffi.load(runtime, true)
local example = ffi.load(library)
```
