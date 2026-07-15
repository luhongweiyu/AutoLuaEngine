---
params: "url: string, data: string, contentType: string, timeout: integer"
returns: "string?"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 文本 POST。

**语法：** `LuaEngine.httpPostData(url, data, contentType, timeout)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `url` | `string` | 是 | 网络请求地址。 |
| `data` | `string` | 是 | 具体取值和组合规则见下方详细说明。 |
| `contentType` | `string` | 是 | 具体取值和组合规则见下方详细说明。 |
| `timeout` | `integer` | 是 | 超时时间，单位见详细说明。 |

| 返回值 | 说明 |
|---|---|
| `string?` | 返回字符串；系统未提供该信息时可能为 nil。 |

**详细说明：**

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
