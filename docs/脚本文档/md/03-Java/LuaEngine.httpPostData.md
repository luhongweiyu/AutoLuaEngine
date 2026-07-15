---
params: "url: string, data: string, contentType: string, timeout: integer"
returns: "string?"
---

文本数据 HTTP POST。

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
