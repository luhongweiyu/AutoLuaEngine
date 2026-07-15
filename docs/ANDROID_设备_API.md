# Android 设备 API（实现说明）

脚本侧完整 API、参数、返回值与示例见 [脚本文档](脚本文档.md)（分类「设备」）。

## 实现路径

```text
m.* -> Lua HostApi -> system_c_api -> core/api/device_api -> AndroidBridge -> Android 平台 / RootDaemon
```

需要最高权限的操作只走已经常驻的 `RootDaemon`。脚本执行过程中不会重复申请 `su`、不会
尝试无障碍或普通权限备用路线。读取类 API 尽量使用 Android Framework；Framework 不公开
的信息会返回 `nil`，不会伪造值。

所有 Lua 入口只位于 `m.*`，本批不增加 `lr.*`、`cd.*` 或新的全局函数。

## C ABI

外部 so、未来 Go 和 JS 通过 `engine_getApi()` 取得 `EngineApi*`，再调用：

```c
const EngineApi* api = engine_getApi();
const EngineDeviceApi* device = api->getDeviceApi();

int is_front = device->appIsFront("com.example.app");
const char* brand = device->getBrand();
```

`EngineDeviceApi` 的全部声明位于：

```text
engines/android/app/src/main/cpp/core/system_c_api.h
```

C ABI 的状态型函数返回 `1`/`0`；字符串、JSON 返回值和 `engine_deviceLastError()` 的指针
都由当前调用线程持有，只读且不需要释放。Lua 层为了保留脚本方法语义，会将
`runApp`、`installApk`、`lockScreen` 等转换为无返回值。
