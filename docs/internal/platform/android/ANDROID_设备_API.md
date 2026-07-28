# Android 设备 API（实现说明）

脚本侧完整 API、参数、返回值与示例见
[脚本文档](../../../public/脚本文档.md)（分类「设备」）。

兼容加密、网络、图色、文件、OpenCV 和无障碍节点的分层见
[Android Lua 兼容层](ANDROID_Lua_兼容层.md)。

## 实现路径

```text
m.* -> Lua HostApi -> system_c_api -> core/api/device_api -> AndroidBridge -> Android 平台 / RootDaemon
```

需要最高权限的操作只走已经常驻的 `RootDaemon`。脚本执行过程中不会重复申请 `su`、不会
尝试无障碍或普通权限备用路线。读取类 API 尽量使用 Android Framework；Framework 不公开
的信息会返回 `nil`，不会伪造值。

设备能力以 `m.*` 为主契约；默认命名空间会把一级成员导出为同名全局函数。`lr` / `cd`
兼容表复用语义相同的 `m` 成员，确有历史差异的入口由兼容层覆盖，不能复制另一套平台实现。

文本剪贴板由 `m.readPasteboard()` / `m.writePasteboard(text[, kind])` 提供；默认 `m` API 会把
一级成员导出为同名全局函数。实现只使用 Application Context 的 Android `ClipboardManager`：
读取第一条 `ClipData` 文本，写入使用 `ClipData.newPlainText(...)`。不需要 Root，也不会添加
Root、无障碍或历史兼容后备路线；Android 12 及以上的后台剪贴板限制按系统实际结果处理。

## C ABI

外部 so、未来 Go 和 JS 通过 `engine_getApi()` 取得 `EngineApi*`，再调用：

```c
const EngineApi* api = engine_getApi();
const EngineDeviceApi* device = api->getDeviceApi();

int is_front = device->appIsFront("com.example.app");
const char* brand = device->getBrand();
const char* value_json = device->callJson("device.isDebug", "{}");
```

`EngineDeviceApi` 的全部声明位于：

```text
engines/android/app/src/main/cpp/core/system_c_api.h
```

C ABI 的状态型函数返回 `1`/`0`；字符串、JSON 返回值和 `engine_deviceLastError()` 的指针
都由当前调用线程持有，只读且不需要释放。Lua 层为了保留脚本方法语义，会将
`runApp`、`installApk`、`lockScreen` 等转换为无返回值。

ABI 20 在 `EngineDeviceApi` 尾部追加 `callJson`，供经过引擎路由和白名单分发的平台扩展使用。
Lua 的内部 HostApi 会把 JSON 值转换成 Lua 值，并把失败统一转换为 `nil, errorMessage`；
脚本 API 不能直接绕过兼容层调用任意 Java 方法。
