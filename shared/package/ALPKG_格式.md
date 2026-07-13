# ALPKG 脚本包格式

`.alpkg` 是 ZIP 容器，用于把一个脚本项目递归打包成单文件。第一版目标是 Android + Lua
5.4.8，运行端不解压到磁盘。

## 项目配置

项目根目录使用 `alpkg.json`：

```json
{
  "entry": "main.lua",
  "exclude": [".git/", ".vscode/", "dist/", "*.bak"]
}
```

- `entry`：必填，入口 Lua 文件，相对项目根目录。
- `exclude`：可选，支持 `目录/`、`*.后缀`、`相对路径/**` 规则。

首次打包时如果缺少 `alpkg.json`，打包器会自动创建上面的默认配置，再继续打包。

## ZIP 目录

```text
manifest.json                 明文包清单
code/main.luac.enc             加密的去调试 Lua 字节码
code/modules/task.luac.enc     加密的去调试 Lua 字节码
ui/index.html                 原样资源
images/icon.png               原样资源
```

Lua 源码文件不会写入包内。其他资源保持原格式，以便 HTML、图片和配置按需读取。

## manifest.json

```json
{
  "format": "alpkg",
  "formatVersion": 1,
  "lua": {
    "version": "5.4.8",
    "integerBytes": 8,
    "numberBytes": 8,
    "byteOrder": "little"
  },
  "entry": "main.lua",
  "files": {
    "main.lua": {
      "kind": "lua",
      "path": "code/main.luac.enc",
      "nonce": "Base64 24 字节随机 nonce",
      "tag": "Base64 16 字节认证标签"
    },
    "ui/index.html": {
      "kind": "resource",
      "path": "ui/index.html"
    }
  }
}
```

每份 Lua 字节码使用 `XChaCha20-Poly1305` 独立加密。关联数据固定为其项目相对路径，
因此替换、改名或挪动密文都会导致认证失败。

## 运行规则

1. App 识别 `.alpkg` 并把真实包路径传给 `libengine.so`。
2. SO 读取 `manifest.json`，校验格式和 Lua ABI 信息。
3. SO 按需读取 ZIP 中的 Lua 密文，验证并解密后直接 `lua_load`。
4. `require`、`dofile`、`loadfile` 从包内读取 Lua 模块；相对 HTML 文件通过包内容提供器给
   WebView 读取。
5. 解密后的字节码不写入临时文件，使用完立即清零释放。

包内 HTML 使用普通相对资源路径即可，例如 `ui/index.html` 中的
`<link href="style.css">`、`<script src="app.js">`、`<img src="images/logo.png">` 都会继续从
同一个 `.alpkg` 读取，无需解压资源目录。

脚本需要直接读取原始资源时使用 `m.read_alpkg_file(relativePath)`，例如读取 JSON、CSS、
图片或任意二进制数据。该接口只读取 `manifest.json` 中 `kind: "resource"` 的项目相对
路径，不回退到包外文件系统，也不能读取 Lua 加密字节码。资源内容只返回到脚本内存，不会
写入缓存目录。

第一版为离线通用包，密钥仍随运行端存在，只能提高提取成本而不能提供绝对保密。后续服务器
授权模式可替换包密钥来源，不改变 ZIP 和 manifest 格式。
