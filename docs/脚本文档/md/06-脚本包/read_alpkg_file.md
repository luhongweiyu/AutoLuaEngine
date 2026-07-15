---
params: "path: string"
returns: "string | nil, string?"
---

```lua
local css, errorMessage = m.read_alpkg_file("ui/style.css")
if not css then
    error(errorMessage)
end

local configText = assert(m.read_alpkg_file("data/config.json"))
local imageBytes = assert(m.read_alpkg_file("images/logo.png"))
```

`m.read_alpkg_file(path:string)` 返回二进制安全的 Lua `string`，适用于 CSS、JSON、HTML、图片和
其他非 Lua 文件。它只接受当前 `.alpkg` 的项目相对路径，只读取 `manifest.json` 中登记为
`resource` 的条目；不会解压到缓存目录、不会回退到包外文件，也不能读取加密 Lua 字节码。
读取失败返回 `nil, errorMessage:string`；普通 `.lua` 脚本调用时也会返回失败。

打包与格式说明见 [ALPKG 格式](../shared/package/ALPKG_格式.md)。


读取当前 `.alpkg` 的原始资源。成功返回文件内容字符串；失败返回 `nil, errorMessage`。
