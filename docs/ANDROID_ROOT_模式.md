# Android Root 模式

当前 Root 模式为已经完成的截图、Root 输入注入、输入法切换、设备系统控制和物理音量键监听服务。

## 当前行为

- App 主进程在启动或切换到 Root 模式时，通过 `RootDaemonService` 启动一次 RootDaemon；启动前
  会先用已有令牌确认当前主进程持有的 daemon，已存在时不会再次执行 `su`。
- RootDaemon 由 `su -c app_process` 创建，监听仅限本机的认证 socket；`:engine` 只连接它，绝不执行 `su`。
- RootDaemon 端口由当前 Android 应用 UID 映射生成，不同安装包使用不同回环端口。保留旧版
  `com.autolua.engine` 或其他测试包时，不会抢占 小鱼精灵 的 RootDaemon 端口。
- 未激活图片屏幕且 `engine_getScreenPixels` 缓存未命中时，通过当前 Android root 截图路线
  获取 RGBA 点阵；图片屏幕激活期间完全不进入 Root 截图路线。
- `touchDown`、`touchMove`、`touchUp`、`keyDown`、`keyUp`、`keyPress`、`inputText`
  通过 RootDaemon 注入，不为每条命令拉起 `input` 外部进程。
- `engine_imeLock` / `engine_imeUnlock` 通过 RootDaemon 执行系统输入法切换；
  `engine_imeSetText` 直接调用已活动的应用输入法，不执行 Root 命令。
- `m.exec`、前台应用查询、启动/停止应用、APK 安装、飞行模式、蓝牙、Wi-Fi、息屏和拨号
  通过同一个 RootDaemon 会话执行。`exec` 仅返回命令输出，不根据退出码做兜底或二次执行。
- RootDaemon 首次启动时会一次性授予本应用已声明的电话状态、拨号和短信运行时权限；脚本
  调用这些 API 时不会再执行权限申请或 `pm grant`。
- 设置中的“音量键控制”默认开启。RootDaemon 直接持续读取 `/dev/input/event*`，音量加运行
  当前选中的脚本，音量减停止脚本；监听过程不启动 `getevent` 等外部进程。
- 切换“音量键控制”只启停现有监听连接，绝不检查 Root、创建 RootDaemon 或执行 `su`。若
  当前 RootDaemon 已不存在，监听连接直接结束；重新打开 App 或重新切换 Root 模式才会按该
  模式重新准备 RootDaemon。
- RootDaemonService 被系统按 `START_STICKY` 重建时同样只恢复已有音量键监听，不会执行
  `su`。未知 Service Action 也不会进入 Root 初始化路径。
- RootDaemon 正常初始化完成时不弹出“已就绪”提示，也不发送脚本状态广播；状态页通过
  `device.info.rootRuntimeReady` 展示实时状态。初始化失败才会主动提示错误。
- 失败时直接返回错误，不做路线兜底。

强制停止 `:engine` 时，只会关闭引擎与 RootDaemon 的 socket。RootDaemon 仍由 App 主进程
持有，因此下次运行脚本会直接重连，不会重新申请 Root 授权。关闭 Root 模式或 App 主进程结束
时才关闭 RootDaemon。

非 Root 模式下，已启用的 `AutomationAccessibilityService` 接收同样的全局音量键事件。
关闭“音量键控制”后，Root 订阅连接和无障碍按键处理都会立即停止生效。

## 当前范围

已完成的 Root 能力统一按 `core/api -> system_c_api -> 语言绑定` 分层；尚未实现的
自动化能力继续按这一边界新增，不在 Java 或 Lua 层单独实现业务逻辑。

设备 API 的参数和 Lua 返回值见 [脚本文档 · 设备](脚本文档.md) / [设备实现说明](ANDROID_设备_API.md)。

## 截图接口

```c
int engine_getScreenPixels(int* width, int* height, unsigned char** pixels);
int engine_setScreenPixels(const char* imagePath);
int engine_restoreScreenPixels();
void engine_keepCapture();
void engine_releaseCapture();
int engine_setCaptureCacheMs(int durationMs);
const char* engine_screenLastError();
int engine_capture(const char* path, const EngineRect* region);
```

Lua 暂时映射为：

```lua
local w, h, pixels = m.getScreenPixels()
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
