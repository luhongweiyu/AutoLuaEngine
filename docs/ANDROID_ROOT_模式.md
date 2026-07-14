# Android Root 模式

当前 Root 模式为已经完成的截图、Root 输入注入、输入法切换和物理音量键监听服务。

## 当前行为

- App 主进程在启动或切换到 Root 模式时，通过 `RootDaemonService` 启动一次 RootDaemon。
- RootDaemon 由 `su -c app_process` 创建，监听仅限本机的认证 socket；`:engine` 只连接它，绝不执行 `su`。
- `engine_capture` 缓存未命中时，通过当前 Android root 截图路线获取 RGBA 点阵。
- `touchDown`、`touchMove`、`touchUp`、`keyDown`、`keyUp`、`keyPress`、`inputText`
  通过 RootDaemon 注入，不为每条命令拉起 `input` 外部进程。
- `engine_imeLock` / `engine_imeUnlock` 通过 RootDaemon 执行系统输入法切换；
  `engine_imeSetText` 直接调用已活动的应用输入法，不执行 Root 命令。
- 设置中的“音量键控制”默认开启。RootDaemon 直接持续读取 `/dev/input/event*`，音量加运行
  当前选中的脚本，音量减停止脚本；监听过程不启动 `getevent` 等外部进程。
- 切换“音量键控制”只启停现有监听连接，绝不检查 Root、创建 RootDaemon 或执行 `su`。若
  当前 RootDaemon 已不存在，监听连接直接结束；重新打开 App 或重新切换 Root 模式才会按该
  模式重新准备 RootDaemon。
- 失败时直接返回错误，不做路线兜底。

强制停止 `:engine` 时，只会关闭引擎与 RootDaemon 的 socket。RootDaemon 仍由 App 主进程
持有，因此下次运行脚本会直接重连，不会重新申请 Root 授权。关闭 Root 模式或 App 主进程结束
时才关闭 RootDaemon。

非 Root 模式下，已启用的 `AutomationAccessibilityService` 接收同样的全局音量键事件。
关闭“音量键控制”后，Root 订阅连接和无障碍按键处理都会立即停止生效。

## 当前范围

已完成的 Root 能力统一按 `core/api -> system_c_api -> 语言绑定` 分层；尚未实现的
自动化能力继续按这一边界新增，不在 Java 或 Lua 层单独实现业务逻辑。

## 截图接口

```c
int engine_capture(int* width, int* height, unsigned char** pixels);
void engine_keepCapture();
void engine_releaseCapture();
int engine_setCaptureCacheMs(int durationMs);
const char* engine_captureLastError();
```

Lua 暂时映射为：

```lua
local w, h, pixels = m.capture()
```

## 输入与输入法接口

```c
int engine_touchDown(int id, int x, int y);
int engine_touchMove(int id, int x, int y);
int engine_touchUp(int id);
int engine_keyDown(const char* keyCode);
int engine_keyUp(const char* keyCode);
int engine_keyPress(const char* keyCode);
int engine_inputText(const char* text);
int engine_imeLock();
int engine_imeSetText(const char* text);
int engine_imeUnlock();
```

`engine_inputText` 适合英文、数字和常见符号。需要完整 Unicode 文本时，Lua 使用
`imeLib.lock()`、`imeLib.setText(text)`、`imeLib.unlock()`。
