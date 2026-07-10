# Engine Protocol

IDE / PC 工具与 Android 引擎当前只保留控制类 HTTP JSON 命令。系统自动化能力正在收敛到 `libengine.so` C ABI，旧的系统自动化协议入口已删除。

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

后续如果 IDE 需要直接调系统能力，应优先绑定或转发到 `libengine.so` 的 C ABI，而不是重新增加一套 HTTP 业务 API。

## 当前截图核心

```c
int screen_capture(int* width, int* height, unsigned char** pixels);
```

更多细节见：

```text
docs/ANDROID_SO_截图核心.md
```
