# Engine Protocol

IDE / PC 工具与 Android 引擎当前只保留控制类 HTTP JSON 命令。脚本 API 和可复用
运行时能力的真实逻辑收敛到 `libengine.so/core/api`，对外复用走 `system_c_api`
C ABI。

## 保留命令

```text
script.run
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

## 当前截图核心

```c
int screen_capture(int* width, int* height, unsigned char** pixels);
```

更多细节见：

```text
docs/ANDROID_SO_截图核心.md
```
