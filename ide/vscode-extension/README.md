# AutoLuaEngine VS Code Extension

最小 VS Code 插件雏形，用于通过 Android 引擎的 HTTP JSON-RPC 协议运行 Lua。

当前命令：

- `AutoLuaEngine: Check Connection`
- `AutoLuaEngine: Run Current Lua File`
- `AutoLuaEngine: Pause Script`
- `AutoLuaEngine: Resume Script`
- `AutoLuaEngine: Stop Script`
- `AutoLuaEngine: Drain Logs`

插件激活后会在 VS Code 底部状态栏显示：

- `AutoLua`：检查连接
- `Run Lua`：发送当前编辑器里的 Lua 文件并运行
- `Pause`：请求引擎暂停当前脚本
- `Resume`：请求引擎继续已暂停脚本
- `Stop`：请求引擎停止当前脚本
- `Logs`：读取引擎日志到 `AutoLuaEngine` Output 面板

使用前需要：

1. Android App 已启动
2. 模拟器或真机已连接 adb
3. 插件配置里的 `autolua.adbPath` 指向本机 adb

连接配置：

- `autolua.host`：IDE 连接的主机，默认 `127.0.0.1`
- `autolua.port`：IDE 本机连接端口，默认 `18380`
- `autolua.useAdbForward`：是否自动执行 adb forward，默认 `true`
- `autolua.remotePort`：Android 引擎端口，默认 `18380`

默认连接方式是 `adb forward tcp:18380 tcp:18380`，然后 IDE 访问
`http://127.0.0.1:18380/jsonrpc`。

调试插件时，用 VS Code 打开当前目录 `ide/vscode-extension`，然后按 F5，
选择 `Run AutoLuaEngine Extension` 启动 Extension Development Host。
在打开的新窗口里再打开你的脚本文件夹，底部状态栏按钮会直接可用。

当前插件只做最小闭环，不包含完整编辑器、设备列表或图形化面板。
