# Engine Protocol v0.1

本协议用于 IDE/PC 工具与各平台引擎通讯。

第一版 Android 引擎已实现最小 HTTP JSON-RPC 通讯，后续 VS Code 插件、Qt 工具、Windows/iOS 引擎都应尽量遵守这里的语义。

## 1. 传输方式

第一版后续建议：

```text
HTTP JSON-RPC：普通命令，当前已实现
HTTP JSON-RPC `log.drain`：日志轮询，当前已实现
WebSocket：日志和状态事件，后续实现
HTTP 二进制流：截图像素、图片资源，后续实现
```

Android 调试阶段可通过：

```text
adb forward tcp:18380 tcp:18380
```

连接手机内的本地服务。

当前 Android 默认监听地址：

```text
127.0.0.1:18380
```

连接配置约定：

```text
engineHost：Android 引擎监听地址，第一版固定为 127.0.0.1
enginePort：Android 引擎端口，默认 18380
clientHost：IDE/PC 实际访问地址，adb forward 模式默认 127.0.0.1
clientPort：IDE/PC 实际访问端口，默认 18380
```

第一版默认使用 adb forward，不建议把 Android 端 HTTP 服务直接暴露到局域网。

## 2. 通用请求格式

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "script.run",
  "params": {}
}
```

## 3. 通用响应格式

成功：

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {}
}
```

失败：

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "error": {
    "code": -32000,
    "message": "error message"
  }
}
```

## 4. `script.run`

说明：

- 运行脚本文本
- 第一版只支持 `lua`
- 后续支持 `js`、`go`

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "script.run",
  "params": {
    "language": "lua",
    "code": "print('hello')"
  }
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "status": "finished",
    "message": "task#1 finished: Lua script OK"
  }
}
```

## 5. `script.stop`

说明：

- 请求停止脚本
- 必须是协作停止，不强杀线程

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "method": "script.stop",
  "params": {
    "taskId": 1
  }
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "result": {
    "accepted": true
  }
}
```

## 6. `script.pause`

说明：

- 请求暂停当前脚本
- 必须是协作暂停，不直接挂起系统线程
- 如果脚本正在执行很长的宿主函数，暂停会等宿主函数返回 Lua VM 后生效

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "method": "script.pause",
  "params": {
    "taskId": 1
  }
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "result": {
    "accepted": true,
    "status": "pausing"
  }
}
```

## 7. `script.resume`

说明：

- 请求继续已暂停或正在等待暂停的脚本

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 4,
  "method": "script.resume",
  "params": {
    "taskId": 1
  }
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 4,
  "result": {
    "accepted": true,
    "status": "running"
  }
}
```

## 8. `script.status`

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 5,
  "method": "script.status",
  "params": {
    "taskId": 1
  }
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 5,
  "result": {
    "taskId": 1,
    "status": "finished",
    "result": "Lua script OK",
    "error": null
  }
}
```

当前第一版说明：

- `script.run` 仍是同步执行
- `script.status` 查询当前或最后一个任务
- `taskId = 0` 表示查询最后一个任务
- 任务状态当前可能为：`idle`、`running`、`pausing`、`paused`、`stopping`、`finished`、`failed`

## 9. `device.info`

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 6,
  "method": "device.info",
  "params": {}
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 6,
  "result": {
    "platform": "android",
    "engineVersion": "0.1.0",
    "luaVersion": "Lua 5.4",
    "apiLevel": 34,
    "packageName": "com.autolua.engine",
    "rootModeEnabled": true,
    "rootAvailable": true,
    "rootStatus": {
      "available": true,
      "commandMode": "SU_0_SH_C",
      "suPath": "su",
      "cached": false,
      "cacheExpireAt": 1782280000000,
      "error": "",
      "attempts": []
    },
    "accessibilityEnabled": false,
    "automationMode": "root-first",
    "httpHost": "127.0.0.1",
    "httpPort": 18380
  }
}
```

## 9.1 `device.setRootModeEnabled`

说明：

- 设置 Android Root 模式开关
- 默认开启
- 该设置会持久化，App 界面、脚本和 IDE 查询的是同一份状态
- 关闭后触控、按键、截图不再主动走 root；显式 `root.exec` 仍会尝试 root 命令
- 响应返回设置后的完整 `device.info`

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 7,
  "method": "device.setRootModeEnabled",
  "params": {
    "enabled": true
  }
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 7,
  "result": {
    "platform": "android",
    "engineVersion": "0.1.0",
    "luaVersion": "Lua 5.4",
    "apiLevel": 34,
    "packageName": "com.autolua.engine",
    "rootModeEnabled": true,
    "rootAvailable": true,
    "accessibilityEnabled": false,
    "automationMode": "root-first",
    "httpHost": "127.0.0.1",
    "httpPort": 18380
  }
}
```

## 9.2 `root.exec`

说明：

- 通过 Android root shell 执行命令
- 第一版用于调试和后续 root 文件、进程能力的基础通道
- `timeoutMs` 默认 2500，最大 30000
- 该方法是显式 root 能力，不受界面 Root 模式开关影响

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 8,
  "method": "root.exec",
  "params": {
    "command": "id -u",
    "timeoutMs": 2000
  }
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 8,
  "result": {
    "ok": true,
    "exitCode": 0,
    "stdout": "0\n",
    "stderr": "",
    "timedOut": false,
    "error": ""
  }
}
```

## 9.3 `root.status`

说明：

- 获取 Android root 探测状态
- 用于定位 App 进程是否能拿到 root、命中的 su 路径和 su 参数格式
- `attempts` 会列出最近一轮探测过的 su 命令和输出

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 9,
  "method": "root.status",
  "params": {}
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 9,
  "result": {
    "available": false,
    "commandMode": "NONE",
    "suPath": "",
    "cached": true,
    "cacheExpireAt": 1782280000000,
    "error": "root is not available",
    "attempts": [
      {
        "commandMode": "SU_C",
        "suPath": "su",
        "exitCode": 1,
        "stdout": "",
        "stderr": "permission denied",
        "timedOut": false,
        "error": ""
      }
    ]
  }
}
```

说明：

- `adb shell` 可以 root 不代表 App 进程可以 root，最终以这里的 `available` 为准。
- `device.info` 也会返回同一份 `rootStatus` 摘要。

## 9.4 `root.file.exists`

说明：

- 通过 root shell 判断路径是否存在
- 显式 root 能力，不受 Root 模式开关影响

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 10,
  "method": "root.file.exists",
  "params": {
    "path": "/data/local/tmp/demo.txt"
  }
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 10,
  "result": {
    "exists": true,
    "error": ""
  }
}
```

## 9.5 `root.file.readText`

说明：

- 通过 root shell 读取 UTF-8 文本文件
- `timeoutMs` 默认 2500，最大 30000

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 10,
  "method": "root.file.readText",
  "params": {
    "path": "/data/local/tmp/demo.txt",
    "timeoutMs": 2000
  }
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 10,
  "result": {
    "content": "hello\n"
  }
}
```

## 9.6 `root.file.writeText`

说明：

- 通过 root shell 覆盖写入 UTF-8 文本文件
- `timeoutMs` 默认 2500，最大 30000
- 第一版内部使用 base64 传输文本，减少 shell 转义问题

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 11,
  "method": "root.file.writeText",
  "params": {
    "path": "/data/local/tmp/demo.txt",
    "content": "hello\n中文",
    "timeoutMs": 2000
  }
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 11,
  "result": {
    "ok": true
  }
}
```

## 9.7 `root.file.remove`

说明：

- 通过 root shell 删除指定路径
- 第一版用于文件删除，不承诺递归目录删除

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 12,
  "method": "root.file.remove",
  "params": {
    "path": "/data/local/tmp/demo.txt"
  }
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 12,
  "result": {
    "ok": true
  }
}
```

## 9.8 `root.file.mkdir`

说明：

- 通过 root shell 创建目录
- `recursive` 可选，默认 `true`，等同 `mkdir -p`

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 13,
  "method": "root.file.mkdir",
  "params": {
    "path": "/data/local/tmp/autolua",
    "recursive": true
  }
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 13,
  "result": {
    "ok": true
  }
}
```

## 9.9 `root.file.chmod`

说明：

- 通过 root shell 修改文件或目录权限
- `mode` 必须是 3 或 4 位八进制字符串，例如 `"755"`、`"0644"`

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 14,
  "method": "root.file.chmod",
  "params": {
    "path": "/data/local/tmp/autolua/run.sh",
    "mode": "755"
  }
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 14,
  "result": {
    "ok": true
  }
}
```

## 9.10 `root.process.pidOf`

说明：

- 通过 root shell 查询进程名对应的 PID 列表
- 找不到进程时返回空数组
- 显式 root 能力，不受 Root 模式开关影响

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 15,
  "method": "root.process.pidOf",
  "params": {
    "name": "com.android.settings"
  }
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 15,
  "result": {
    "pids": [1234]
  }
}
```

## 9.11 `root.process.kill`

说明：

- 通过 root shell 结束指定进程
- `target` 可以是 PID 字符串或进程名；也可以传 `pid` 或 `name`
- `signal` 可选，默认 `15`

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 16,
  "method": "root.process.kill",
  "params": {
    "target": "com.example.target",
    "signal": 15
  }
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 16,
  "result": {
    "ok": true
  }
}
```

## 9.12 `app.isInstalled`

说明：

- 判断 Android 包是否已安装

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 17,
  "method": "app.isInstalled",
  "params": {
    "packageName": "com.android.settings"
  }
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 17,
  "result": {
    "installed": true
  }
}
```

## 9.13 `app.open` / `app.start`

说明：

- 启动 Android 应用
- Root 模式开启且 root 可用时优先 root 启动，失败后回退普通 Launcher Intent

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 18,
  "method": "app.open",
  "params": {
    "packageName": "com.android.settings"
  }
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 18,
  "result": {
    "ok": true
  }
}
```

## 9.14 `app.stop`

说明：

- 强停 Android 应用
- 当前通过 root `am force-stop` 实现

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 19,
  "method": "app.stop",
  "params": {
    "packageName": "com.example.target"
  }
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 19,
  "result": {
    "ok": true
  }
}
```

## 9.15 `app.clearData`

说明：

- 清理指定 Android 应用数据
- 当前通过 root `pm clear` 实现
- root 不可用或包名无效时返回 `ok: false`

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 20,
  "method": "app.clearData",
  "params": {
    "packageName": "com.example.target"
  }
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 20,
  "result": {
    "ok": true
  }
}
```

## 9.16 `app.grant`

说明：

- 给指定 Android 应用授予权限
- 当前通过 root `pm grant` 实现
- root 不可用、包名无效、权限名无效或目标权限不可授予时返回 `ok: false`

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 21,
  "method": "app.grant",
  "params": {
    "packageName": "com.example.target",
    "permission": "android.permission.CAMERA"
  }
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 21,
  "result": {
    "ok": true
  }
}
```

## 9.17 `app.revoke`

说明：

- 撤销指定 Android 应用权限
- 当前通过 root `pm revoke` 实现

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 22,
  "method": "app.revoke",
  "params": {
    "packageName": "com.example.target",
    "permission": "android.permission.CAMERA"
  }
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 22,
  "result": {
    "ok": true
  }
}
```

## 9.18 `app.install`

说明：

- 安装 Android APK
- 当前通过 root `pm install` 实现
- `path` 和 `apkPath` 二选一，推荐使用 `path`
- `replace` 默认 `true`，表示覆盖安装
- root 不可用、APK 路径无效或安装失败时返回 `ok: false`

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 23,
  "method": "app.install",
  "params": {
    "path": "/sdcard/Download/demo.apk",
    "replace": true
  }
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 23,
  "result": {
    "ok": true
  }
}
```

## 9.19 `app.uninstall`

说明：

- 卸载指定 Android 应用
- 当前通过 root `pm uninstall` 实现
- `keepData` 默认 `false`；为 `true` 时保留应用数据
- root 不可用、包名无效或卸载失败时返回 `ok: false`

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 24,
  "method": "app.uninstall",
  "params": {
    "packageName": "com.example.target",
    "keepData": false
  }
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 24,
  "result": {
    "ok": true
  }
}
```

## 9.20 `app.disable`

说明：

- 冻结指定 Android 应用
- 当前通过 root `pm disable-user --user 0` 实现
- root 不可用、包名无效或冻结失败时返回 `ok: false`

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 25,
  "method": "app.disable",
  "params": {
    "packageName": "com.example.target"
  }
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 25,
  "result": {
    "ok": true
  }
}
```

## 9.21 `app.enable`

说明：

- 解冻指定 Android 应用
- 当前通过 root `pm enable` 实现
- root 不可用、包名无效或解冻失败时返回 `ok: false`

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 26,
  "method": "app.enable",
  "params": {
    "packageName": "com.example.target"
  }
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 26,
  "result": {
    "ok": true
  }
}
```

## 9.22 `key.press`

说明：

- 执行 Android 通用按键码
- Root 模式开启且 root 可用时优先使用 root `input keyevent`
- `keyCode = 4` 和 `keyCode = 3` 可在 root 失败后回退无障碍返回/Home
- 其他 keyCode 当前依赖 root

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 27,
  "method": "key.press",
  "params": {
    "keyCode": 66
  }
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 27,
  "result": {
    "ok": true
  }
}
```

## 9.23 `input.text`

说明：

- 向当前焦点输入框输入文本
- Android 第一版先使用 root `input text`
- root `input text` 失败后自动回退剪贴板 + root 粘贴
- Root 模式关闭、root 不可用或当前焦点控件无法接收输入时返回 `ok: false`

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 28,
  "method": "input.text",
  "params": {
    "text": "hello world"
  }
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 28,
  "result": {
    "ok": true
  }
}
```

## 9.24 `input.pasteText`

说明：

- 通过剪贴板向当前焦点控件粘贴文本
- 适合中文、换行和复杂符号
- 该方法会覆盖系统剪贴板内容
- 依赖 root `KEYCODE_PASTE` 和当前焦点控件的粘贴能力

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 29,
  "method": "input.pasteText",
  "params": {
    "text": "中文输入\n第二行"
  }
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 29,
  "result": {
    "ok": true
  }
}
```

## 10. 日志事件

WebSocket 事件：

```json
{
  "event": "log",
  "params": {
    "taskId": 1,
    "level": "info",
    "message": "hello",
    "time": 1782280000000
  }
}
```

## 10.1 `log.drain`

说明：

- 轮询读取脚本日志
- 第一版用于 PC/IDE 获取 `print`、`log.print` 输出
- 后续可以升级为 WebSocket 实时推送

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 6,
  "method": "log.drain",
  "params": {
    "afterId": 0
  }
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 6,
  "result": {
    "lastId": 1,
    "entries": [
      {
        "id": 1,
        "level": "info",
        "message": "hello"
      }
    ]
  }
}
```

## 10.2 `screen.capture`

说明：

- 请求当前屏幕截图
- Android 优先使用 root 原始 `screencap`，失败后回退 MediaProjection 授权
- 第一版返回图片句柄和基础元信息
- 当前 Android HTTP JSON-RPC 已实现该方法
- 像素数据保留在引擎内存中，供后续 image API 使用，不通过协议反复传输大块数据
- 当前高频点阵读取应优先在脚本侧通过 `image.getPixel` / `image.getPixels` 操作图片句柄，避免把像素数据经 HTTP 来回传输

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 5,
  "method": "screen.capture",
  "params": {}
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 5,
  "result": {
    "id": 1,
    "type": "image",
    "width": 1080,
    "height": 2220,
    "rowStride": 4320,
    "pixelStride": 4,
    "byteLength": 9590400,
    "format": "rgba8888"
  }
}
```

失败示例：

```json
{
  "jsonrpc": "2.0",
  "id": 5,
  "error": {
    "code": -32000,
    "message": "screen capture permission is not granted"
  }
}
```

## 10.3 `image.release`

说明：

- 释放 `screen.capture` 返回的 native 图片句柄
- IDE/PC 工具拿到截图句柄后，应在使用完毕后主动释放
- 释放不存在的句柄会返回参数错误

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 7,
  "method": "image.release",
  "params": {
    "id": 1
  }
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 7,
  "result": {
    "released": true
  }
}
```

## 11. 状态事件

WebSocket 事件：

```json
{
  "event": "task.status",
  "params": {
    "taskId": 1,
    "status": "failed",
    "error": "script stopped"
  }
}
```
