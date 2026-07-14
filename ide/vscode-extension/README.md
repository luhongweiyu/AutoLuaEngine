# 小鱼精灵 VS Code Extension

最小 VS Code 插件雏形，用于通过 Android 引擎的 HTTP JSON-RPC 协议运行 Lua。

当前命令：

- `小鱼精灵: Check Connection`
- `小鱼精灵: Run Current Lua File`
- `小鱼精灵: Pause Script`
- `小鱼精灵: Resume Script`
- `小鱼精灵: Stop Script`
- `小鱼精灵: Drain Logs`
- `小鱼精灵: Package Current Project`

插件激活后会在 VS Code 底部状态栏显示：

- `xiaoyv`：检查连接
- `Run Lua`：发送当前编辑器里的 Lua 文件并运行
- `Pause`：请求引擎暂停当前脚本
- `Resume`：请求引擎继续已暂停脚本
- `Stop`：请求引擎停止当前脚本
- `Logs`：读取引擎日志到 `小鱼精灵` Output 面板
- `打包`：根据工作区根目录的 `alpkg.json` 生成 `.alpkg` 脚本包

打包项目根目录使用：

```json
{
  "entry": "main.lua",
  "exclude": ["dist/", "*.bak"]
}
```

配置不存在时，打包器会自动创建上述默认 `alpkg.json` 后继续执行。

默认调用仓库内的 `tools/pack/build/xiaoyv_pack.exe`。首次尚未构建时，插件会调用
`tools/pack/构建打包器.ps1 -Release`；也可以通过 `xiaoyv.packToolPath` 指定独立打包器路径。

使用前需要：

1. Android App 已启动
2. 模拟器或真机已连接 adb
3. 插件配置里的 `xiaoyv.adbPath` 指向本机 adb

连接配置：

- `xiaoyv.host`：IDE 连接的主机，默认 `127.0.0.1`
- `xiaoyv.port`：IDE 本机连接端口，默认 `18380`
- `xiaoyv.useAdbForward`：是否自动执行 adb forward，默认 `true`
- `xiaoyv.remotePort`：Android 引擎端口，默认 `18380`

默认连接方式是 `adb forward tcp:18380 tcp:18380`，然后 IDE 访问
`http://127.0.0.1:18380/jsonrpc`。

调试插件时，用 VS Code 打开当前目录 `ide/vscode-extension`，然后按 F5，
选择 `Run 小鱼精灵 Extension` 启动 Extension Development Host。
在打开的新窗口里再打开你的脚本文件夹，底部状态栏按钮会直接可用。

当前插件只做最小闭环，不包含完整编辑器、设备列表或图形化面板。
