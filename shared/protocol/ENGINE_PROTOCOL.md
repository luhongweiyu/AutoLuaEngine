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

## 6. `script.status`

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 3,
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
  "id": 3,
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

## 7. `device.info`

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 4,
  "method": "device.info",
  "params": {}
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 4,
  "result": {
    "platform": "android",
    "engineVersion": "0.1.0",
    "luaVersion": "Lua 5.4",
    "apiLevel": 34,
    "packageName": "com.autolua.engine",
    "httpPort": 18380
  }
}
```

## 8. 日志事件

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

## 8.1 `log.drain`

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

## 8.2 `screen.capture`

说明：

- 请求当前屏幕截图
- Android 需要用户先完成 MediaProjection 授权
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

## 8.3 `image.release`

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

## 9. 状态事件

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
