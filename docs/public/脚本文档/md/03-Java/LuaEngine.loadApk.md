---
params: "nameOrPath: string"
returns: "userdata?"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 加载插件。

**语法：** `LuaEngine.loadApk(nameOrPath)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `nameOrPath` | `string` | 是 | 具体取值和组合规则见下方详细说明。 |

| 返回值 | 说明 |
|---|---|
| `userdata?` | 具体字段、特殊值和失败情况见下方详细说明。 |

**详细说明：**

加载 APK/JAR/DEX 插件。

## LuaEngine 兼容方法（原文）

```lua
LuaEngine.getContext()
LuaEngine.httpGet(url, headers [, timeout])
LuaEngine.httpPost(url, params, headers [, timeout])
LuaEngine.httpPostData(url, data, contentType, timeout)
LuaEngine.loadApk(nameOrAbsolutePath)
LuaEngine.getExtensionPath(relativePath)
```

- HTTP 超时单位为秒，成功返回响应字符串，失败返回 `nil`。
- `loadApk` 支持绝对路径、assets 根目录名称和 `assets/plugins` 名称。
- `loadApk` 成功返回 `ApkLoader`，失败返回 `nil`；APK 会被暂存为私有 `base.apk`，并解出
  其中的 `lib/<abi>/*.so`。因此旧代码可从 `loader.getApkPath()` 把结尾的 `base.apk`
  替换为 `lib/` 后定位同 ABI 的原生库。
- 该方法不自动加载 native 库。需要 FFI 时由脚本按依赖顺序调用 `ffi.load`；C++ 运行时等需要被
  后续库解析的依赖应使用 `ffi.load(path, true)` 全局加载。
