# ALPKG 打包工具

项目根目录使用 `alpkg.json`：

```json
{
  "entry": "main.lua",
  "exclude": [".git/", ".vscode/", "dist/", "*.bak"]
}
```

首次打包时如果缺少 `alpkg.json`，打包器会自动创建上述默认配置，然后继续打包。

构建一次打包器：

```powershell
.\构建打包器.ps1 -Release
```

打包项目：

```powershell
.\打包脚本包.ps1 -ProjectDirectory 'E:\我的脚本项目'
```

默认输出为 `项目目录\dist\项目名.alpkg`。Lua 文件会编译为去调试信息的 Lua 5.4.8
字节码并加密；其他文件原样写入 ZIP 容器。包内 HTML 可直接按普通相对路径引用 CSS、
JavaScript、图片和其他资源。Lua 脚本也可通过
`m.read_alpkg_file("项目相对路径")` 读取已打包的原始资源；该读取不会回退到包外文件。
