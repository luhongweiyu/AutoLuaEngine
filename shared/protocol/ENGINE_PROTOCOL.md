# 引擎通讯协议

IDE、VSCode 插件和 PC 工具通过 HTTP 连接 Android `EngineHttpServer`。服务监听设备引擎
端口，使用线程池支持多个客户端并发连接。ADB 客户端各自建立独立端口转发，局域网客户端
各自直连设备，不通过其他 IDE 或工具中转。Qt 工具使用动态本机端口，VSCode 使用配置端口。

脚本 API 和可复用运行时能力的真实逻辑仍收敛到 `libengine.so/core/api`，对外复用走
`system_c_api` C ABI。HTTP 只承载调试控制、工具数据传输和 Android UI 边界命令。

## HTTP 入口

```text
GET  /health             引擎服务健康检查
POST /jsonrpc            控制命令
GET  /tool/screenshot    获取当前设备截图原始帧
PUT  /tool/image         上传一张待投影图片
```

`/jsonrpc` 请求使用 JSON-RPC 2.0：

```json
{"jsonrpc":"2.0","id":1,"method":"script.status","params":{}}
```

## 保留命令

```text
script.run
script.runPackage
script.stop
script.pause
script.resume
script.status
log.drain
device.info
device.setRootModeEnabled
```

后续如果 IDE、插件或其他语言运行时需要直接调脚本 API，应绑定或转发到
`libengine.so` 的 C ABI，不重新用 HTTP 实现一套脚本业务逻辑。

`script.runPackage` 接收：

```json
{
  "packagePath": "/storage/emulated/0/xiaoyv/scripts/demo.alpkg"
}
```

它只接受共享存储中的 `.alpkg` 文件。ZIP 解析、manifest 校验、Lua 字节码认证解密和
执行均在 `libengine.so` 内完成。

## 工具截图

`GET /tool/screenshot` 返回 `application/x-xiaoyv-rgba`：

```text
offset  size  内容
0       4     ASCII "XYVF"
4       4     little-endian int32 width
8       4     little-endian int32 height
12      ...   width * height * 4 字节 RGBA8888
```

该入口直接复制 `libengine.so` 当前截图帧，不进行 PNG 编码或磁盘 IO。

## 工具投影

上传图片：

```text
PUT /tool/image
Content-Type: image/png 或设备可解码的图片类型
Body: 图片二进制
```

响应：

```json
{"fileId":"随机图片标识.image"}
```

打开图片：

```json
{"jsonrpc":"2.0","id":2,"method":"viewer.openImage","params":{"fileId":"..."}}
```

投影协议只包含上传和打开。没有关闭、刷新、状态查询接口；用户在设备上按返回键退出，
重新投影就是再次执行上传和打开。

## 截图核心 C ABI

```c
int engine_getScreenPixels(int* width, int* height, unsigned char** pixels);
```

更多细节见：

```text
docs/ANDROID_SO_截图核心.md
```
