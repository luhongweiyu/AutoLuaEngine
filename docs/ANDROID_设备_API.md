# Android 设备 API

本页描述小鱼精灵 Android 引擎当前已实现的设备、应用和系统控制 API。所有 Lua 入口只
位于 `m.*`，本批不增加 `lr.*`、`cd.*` 或新的全局函数。

实现路径固定为：

```text
m.* -> Lua HostApi -> system_c_api -> core/api/device_api -> AndroidBridge -> Android 平台 / RootDaemon
```

需要最高权限的操作只走已经常驻的 `RootDaemon`。脚本执行过程中不会重复申请 `su`、不会
尝试无障碍或普通权限备用路线。读取类 API 尽量使用 Android Framework；Framework 不公开
的信息会返回 `nil`，不会伪造值。

## 应用与脚本

| 函数 | 参数 | 返回值 | 说明 |
|---|---|---|---|
| `m.appIsFront(packageName)` | `packageName: string` | `boolean` | 指定包是否为当前前台应用，需要 RootDaemon 查询系统窗口状态。 |
| `m.appIsRunning(packageName)` | `packageName: string` | `boolean` | 指定包的主进程是否存在，需要 RootDaemon。 |
| `m.frontAppName()` | 无 | `string?` | 当前前台应用包名。 |
| `m.getCurrentActivity()` | 无 | `string?` | 当前前台 Activity 的完整类名。 |
| `m.runApp(packageName[, componentName[, isOpenBySuper]])` | `string, string?, boolean?` | 无 | 启动应用；有组件名时精确启动，否则打开启动入口。当前 Root 引擎始终以最高权限执行。 |
| `m.stopApp(packageName)` | `packageName: string` | 无 | 强制停止指定应用。 |
| `m.runIntent(intent)` | `intent: table` | `boolean` | 打开 Android Intent。支持 `action`、`uri`、`data`、`packageName`、`extra`。 |
| `m.installApk(apkPath)` | `apkPath: string` | 无 | 通过 RootDaemon 执行 APK 安装。 |
| `m.getInstalledApk()` | 无 | `table` | 已安装 APK 路径数组。 |
| `m.getInstalledApps()` | 无 | `table` | 已安装包名数组。 |
| `m.getInsallAppInfos()` | 无 | `table` | 已安装应用详情数组。函数名保持设备方法文档中的拼写。 |
| `m.getApkVerInt()` | 无 | `integer` | 当前小鱼精灵 APK 的 `versionCode`。 |
| `m.exec(cmd[, isRet])` | `cmd: string, isRet: boolean?` | `string?` 或无 | 以 RootDaemon 权限执行 shell；`isRet` 默认 `true`，返回合并输出；为 `false` 时不返回结果。命令退出码由脚本根据输出自行判断。 |
| `m.exitScript()` | 无 | 无 | 立即停止当前顶层脚本，后续 Lua 语句不会执行。 |
| `m.getXiaoyvApi()` | 无 | `integer` | `EngineApi*` 的内存地址，供 FFI 或外部 so 使用稳定 C ABI。 |

`m.getInsallAppInfos()` 中每项包含：`packageName`、`appName`、`versionName`、`versionCode`、
`apkPath`、`systemApp`。

`m.runIntent` 示例：

```lua
m.runIntent({
    action = "android.settings.SETTINGS",
})

m.runIntent({
    action = "android.intent.action.VIEW",
    data = "https://example.com",
    extra = { source = "xiaoyv" },
})
```

## 设备信息

| 函数 | 参数 | 返回值 | 说明 |
|---|---|---|---|
| `m.getBatteryLevel()` | 无 | `integer` | 电量百分比，无法读取时为 `-1`。 |
| `m.getBoard()` | 无 | `string` | `Build.BOARD`。 |
| `m.getBootLoader()` | 无 | `string` | `Build.BOOTLOADER`。 |
| `m.getBrand()` | 无 | `string` | `Build.BRAND`。 |
| `m.getCpuAbi()` | 无 | `string` | 首选 CPU ABI。 |
| `m.getCpuAbi2()` | 无 | `string` | 第二 CPU ABI；设备没有第二 ABI 时为空字符串。 |
| `m.getCpuArch()` | 无 | `integer` | `1` 为 ARM 系列，`0` 为 x86 系列。 |
| `m.getDevice()` | 无 | `string` | `Build.DEVICE`。 |
| `m.getDeviceId()` | 无 | `string?` | Android ID。 |
| `m.getDisplayDpi()` | 无 | `integer` | 真实显示 DPI。 |
| `m.getDisplayInfo()` | 无 | `table` | 屏幕详情，字段见下表。 |
| `m.getDisplayRotate()` | 无 | `integer` | `0`、`1`、`2`、`3` 分别表示 0、90、180、270 度旋转状态。 |
| `m.getDisplaySize()` | 无 | `width: integer, height: integer` | 真实屏幕宽高。 |
| `m.getFingerprint()` | 无 | `string` | `Build.FINGERPRINT`。 |
| `m.getHardware()` | 无 | `string` | `Build.HARDWARE`。 |
| `m.getId()` | 无 | `string` | `Build.ID`。 |
| `m.getManufacturer()` | 无 | `string` | `Build.MANUFACTURER`。 |
| `m.getModel()` | 无 | `string` | `Build.MODEL`。 |
| `m.getNetWorkTime()` | 无 | `string` | 系统当前时间，格式 `yyyy-MM-dd_HH-mm-ss`。 |
| `m.getOaid()` | 无 | `string?` | OAID；没有 OEM OAID 提供方时返回 `nil`。 |
| `m.getOsVersionName()` | 无 | `string` | Android 系统版本名。 |
| `m.getPackageName()` | 无 | `string` | 小鱼精灵应用包名。 |
| `m.getProduct()` | 无 | `string` | `Build.PRODUCT`。 |
| `m.getRunEnvType()` | 无 | `integer` | `0` 为 Root，`1` 为无障碍，`-1` 为当前无可用运行环境。 |
| `m.getSdPath()` | 无 | `string` | 主共享存储绝对路径。 |
| `m.getSdkVersion()` | 无 | `integer` | Android API Level。 |
| `m.getSensorsInfo()` | 无 | `table` | 传感器详情数组。 |
| `m.getSimSerialNumber()` | 无 | `string?` | SIM 序列号；系统未公开该数据时为 `nil`。 |
| `m.getSubscriberId()` | 无 | `string?` | IMSI；系统未公开该数据时为 `nil`。 |
| `m.getWifiMac()` | 无 | `string?` | Wi-Fi 网卡 MAC；硬件未公开时为 `nil`。 |
| `m.getWorkPath()` | 无 | `string?` | 当前 `.lua` 或 `.alpkg` 所在目录；脚本外调用时为 `nil`。 |

`m.getDisplayInfo()` 返回：

```lua
{
    width = 720,
    height = 1280,
    dpi = 240,
    density = 1.5,
    scaledDensity = 1.5,
    xdpi = 240,
    ydpi = 240,
    rotate = 0,
}
```

`m.getSensorsInfo()` 的每项包含：`name`、`vendor`、`type`、`typeName`、`version`、
`maximumRange`、`resolution`、`power`、`minDelay`。

## 系统控制

| 函数 | 参数 | 返回值 | 说明 |
|---|---|---|---|
| `m.lockScreen()` | 无 | 无 | 保持屏幕常亮，不是锁定设备。 |
| `m.unLockScreen()` | 无 | 无 | 释放 `m.lockScreen()` 获取的常亮锁。 |
| `m.setDisplayPowerOff(isPowerOff)` | `isPowerOff: boolean` | 无 | `true` 息屏运行，`false` 恢复亮屏，需要 RootDaemon。 |
| `m.setAirplaneMode(enabled)` | `enabled: boolean` | 无 | 开关飞行模式，需要 RootDaemon。 |
| `m.setBTEnable(enabled)` | `enabled: boolean` | 无 | 开关蓝牙，需要 RootDaemon。 |
| `m.setWifiEnable(enabled)` | `enabled: boolean` | 无 | 开关 Wi-Fi，需要 RootDaemon。 |
| `m.phoneCall(number[, state])` | `number: string, state: integer?` | 无 | `state=0` 拨号，其他值挂断，需要 RootDaemon。 |
| `m.sendSms(number, content)` | `string, string` | 无 | 发送短信，需要真实电话能力。RootDaemon 初始化时会一次性授予应用所需运行时权限。 |
| `m.vibrate(durationMs)` | `durationMs: integer` | 无 | 震动指定毫秒数。 |

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
