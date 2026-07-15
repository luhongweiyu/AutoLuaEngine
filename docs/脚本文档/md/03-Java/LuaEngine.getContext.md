---
params: "无"
returns: "userdata"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 应用 Context。

**语法：** `LuaEngine.getContext()`

**参数说明：** 无。

| 返回值 | 说明 |
|---|---|
| `userdata` | 具体字段、特殊值和失败情况见下方详细说明。 |

**详细说明：**

返回 Android Application Context。

## LuaEngine 兼容方法（原文）

```lua
LuaEngine.getContext()
LuaEngine.httpGet(url, headers [, timeout])
LuaEngine.httpPost(url, params, headers [, timeout])
LuaEngine.httpPostData(url, data, contentType, timeout)
LuaEngine.loadApk(nameOrAbsolutePath)
```

- HTTP 超时单位为秒，成功返回响应字符串，失败返回 `nil`。
- `loadApk` 支持绝对路径、assets 根目录名称和 `assets/plugins` 名称。
- `loadApk` 成功返回 `ApkLoader`，失败返回 `nil`；`loader.loadClass(className)` 成功返回 Java Class，失败返回 `nil`。
