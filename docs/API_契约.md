# 统一 API 契约

## 1. 设计目标

统一 API 契约用于约束不同脚本语言、不同平台看到的能力语义。

当前 Android native 自动化层已收缩到新的 C ABI 边界，已实现部分以 [ANDROID_SO_截图核心.md](ANDROID_SO_截图核心.md) 为准。本文中旧的触控、按键、输入、图片句柄接口只保留为历史规划记录，不代表当前已暴露接口。

第一版只实现 Android + Lua，但 API 命名和返回规则必须考虑后续 JS、Go、Windows、iOS。

## 2. 返回值规则

Lua API 推荐规则：

- 成功：返回实际结果
- 无返回值成功：返回 `true`
- 失败：返回 `nil, errorMessage`
- 平台不支持：返回 `nil, "unsupported"`

示例：

```lua
local ok, err = m.file.write("/sdcard/test.txt", "hello")
if not ok then
    m.log.print("写入失败: " .. err)
end
```

JS 后续规则：

- 成功：返回实际结果
- 失败：抛出 Error 或返回结构化错误
- 平台不支持：抛出 `UnsupportedError`

## 3. API 命名规范

Lua 第一版分三套命名空间：

- `m`：AutoLuaEngine 自己的正式 API，新脚本必须优先使用它。
- `lr`：懒人精灵兼容层，按懒人精灵文档逐项补齐。
- `cd`：触动精灵兼容层，按触动精灵文档逐项补齐。

新增自动化函数时，函数名优先参考懒人精灵和触动精灵的常见命名。语义一致的能力尽量保持短名和参数顺序接近，例如点击用 `tap`、滑动用 `swipe`、截图用 `capture`。如果两边命名或语义冲突，`m.*` 先采用更清晰、稳定的统一名称，再在 `lr` / `cd` 兼容层分别适配。

C++ 只注册 native `_host` 表；`m/lr/cd/useApi` 都由 Lua runtime 层实现：

```text
engines/android/app/src/main/assets/runtime/api_m.lua
engines/android/app/src/main/assets/runtime/compat_lr.lua
engines/android/app/src/main/assets/runtime/compat_cd.lua
engines/android/app/src/main/assets/runtime/bootstrap.lua
```

默认全局只保留：

- `print(...)`
- `m`
- `lr`
- `cd`
- `useApi(name)`
- `switchApi(name)`

需要兼容旧脚本时，显式调用：

```lua
useApi("lr")
tap(300, 500)
```

`useApi` 会把指定命名空间的一层函数导出到 `_G`。新脚本不应该依赖这些全局导出。

native 侧当前的统一系统能力边界是 `core/SystemApi`。Lua 的 `_host`、后续 JS 的 HostApi、以及 IDE/协议入口都应该先绑定这一层，再由它转到平台实现，不要在不同脚本语言里各自直连 Android 平台桥。

`libengine.so` 当前只对插件/FFI 预留两个稳定 C ABI：`ael_system_version()` 和 `ael_system_capabilities_json()`。具体系统 API 暂不直接开放 C ABI，先通过 HostApi/JSON-RPC 使用，等结构体和内存所有权规则稳定后再扩展。

模块使用小写名：

- `m.log`
- `m.device`
- `m.root`
- `m.app`
- `m.file`
- `m.screen`
- `m.touch`
- `m.image`
- `m.thread`
- `m.key`

函数名使用小驼峰：

- `m.log.print`
- `m.device.info`
- `m.device.setRootModeEnabled`
- `m.root.exec`
- `m.root.file`
- `m.root.process`
- `m.app.isInstalled`
- `m.app.open`
- `m.app.stop`
- `m.app.clearData`
- `m.app.grant`
- `m.app.revoke`
- `m.app.current`
- `m.app.install`
- `m.app.uninstall`
- `m.app.disable`
- `m.app.enable`
- `m.file.read`
- `m.file.exists`
- `m.file.remove`
- `m.screen.capture`
- `m.touch.tap`
- `m.input.text`
- `m.input.pasteText`
- `m.key.isAccessibilityEnabled`
- `m.key.press`
- `m.key.back`
- `m.key.home`
- `m.image.getPixel`
- `m.image.getPixels`
- `m.image.findColor`

Lua 不默认暴露全局 `sleep(ms)`。新脚本使用 `m.sleep(ms)`；兼容脚本可以通过 `useApi("lr")` 或 `useApi("cd")` 暴露对应函数。

## 4. HostApi v0.1

当前已注册：

- 全局 `print(...)`
- 全局 `m`、`lr`、`cd`
- 全局 `useApi(name)` / `switchApi(name)`
- `m.sleep(ms)`
- `m.log.print(text)`
- `m.device.info()`
- `m.device.setRootModeEnabled(enabled)` / `m.setRootModeEnabled(...)`
- `m.device.screenState()` / `m.screenState()`
- `m.device.wake()` / `m.wakeDevice()`
- `m.device.sleep()` / `m.sleepDevice()`
- `m.device.battery()` / `m.battery()`
- `m.device.rotation()` / `m.rotation()`
- `m.device.setRotation(rotation, locked)` / `m.setRotation(...)`
- `m.device.settings.get/put/delete(...)` / `m.getSetting(...)` / `m.putSetting(...)` / `m.deleteSetting(...)`
- `m.device.prop.get/set(...)` / `m.getProp(...)` / `m.setProp(...)`
- `m.device.display.info/setSize/resetSize/setDensity/resetDensity/setBrightness/setAutoBrightness(...)`
- `m.displayInfo()` / `m.setDisplaySize(...)` / `m.resetDisplaySize()` / `m.setDisplayDensity(...)` / `m.resetDisplayDensity()` / `m.setBrightness(...)` / `m.setAutoBrightness(...)`
- `m.root.exec(command, timeoutMs)` / `m.rootExec(...)`
- `m.root.status()` / `m.rootStatus()`
- `m.root.file.exists(path)` / `m.rootFileExists(...)`
- `m.root.file.readText(path, timeoutMs)` / `m.rootReadText(...)`
- `m.root.file.writeText(path, content, timeoutMs)` / `m.rootWriteText(...)`
- `m.root.file.stat(path)` / `m.rootStat(...)`
- `m.root.file.list(path)` / `m.rootList(...)`
- `m.root.file.remove(path, recursive)` / `m.rootRemove(...)`
- `m.root.file.mkdir(path, recursive)` / `m.rootMkdir(...)`
- `m.root.file.chmod(path, mode)` / `m.rootChmod(...)`
- `m.root.file.chown(path, owner)` / `m.rootChown(...)`
- `m.root.process.pidOf(name)` / `m.rootPidOf(...)`
- `m.root.process.list()` / `m.rootProcessList()`
- `m.root.process.info(target)` / `m.rootProcessInfo(...)`
- `m.root.process.stats(target)` / `m.rootProcessStats(...)`
- `m.root.process.kill(target, signal)` / `m.rootKill(...)`
- `m.app.isInstalled(packageName)` / `m.isAppInstalled(...)`
- `m.app.open(packageName)` / `m.app.start(packageName)` / `m.openApp(...)`
- `m.app.stop(packageName)` / `m.stopApp(...)`
- `m.app.clearData(packageName)` / `m.clearAppData(...)`
- `m.app.grant(packageName, permission)` / `m.grantAppPermission(...)`
- `m.app.revoke(packageName, permission)` / `m.revokeAppPermission(...)`
- `m.app.current()` / `m.currentApp()`
- `m.app.install(apkPath, replace)` / `m.installApp(...)`
- `m.app.uninstall(packageName, keepData)` / `m.uninstallApp(...)`
- `m.app.disable(packageName)` / `m.disableApp(...)`
- `m.app.enable(packageName)` / `m.enableApp(...)`
- `m.app.disableComponent(component)` / `m.disableAppComponent(...)`
- `m.app.enableComponent(component)` / `m.enableAppComponent(...)`
- `m.file.read(path)`
- `m.file.write(path, content)`
- `m.file.exists(path)`
- `m.file.remove(path)`
- `m.file.appDataPath(fileName)`
- `m.touch.tap(x, y)` / `m.tap(x, y)`
- `m.touch.swipe(x1, y1, x2, y2, duration)` / `m.swipe(...)`
- `m.input.text(text)` / `m.inputText(text)`
- `m.input.pasteText(text)` / `m.pasteText(text)`
- `m.key.isAccessibilityEnabled()`
- `m.key.press(keyCode)` / `m.pressKey(keyCode)`
- `m.key.back()` / `m.back()`
- `m.key.home()` / `m.home()`
- `m.screen.capture()` / `m.capture()`
- `m.screen.keepCapture()` / `m.keepCapture()`
- `m.screen.releaseCapture()` / `m.releaseCapture()`
- `m.screen.setCaptureCacheMs(ms)` / `m.setCaptureCacheMs(ms)`
- `m.image.getPixel(image, x, y)` / `m.getPixel(...)`
- `m.image.getPixels(image, points)` / `m.getPixels(...)`

`lr` 和 `cd` 当前只做基础能力的部分映射。没有逐项验证前，不允许在文档中标记为完全兼容。

### 4.1 `m.log.print(text)`

说明：

- 输出日志到统一日志通道
- Android 第一版转发到 Logcat
- 后续同时回传 IDE

Lua 示例：

```lua
m.log.print("hello")
```

返回：

```lua
true
```

失败：

```lua
nil, "message"
```

### 4.2 `m.sleep(ms)`

说明：

- 暂停当前脚本任务
- 单位：毫秒
- 第一版可以阻塞当前脚本线程

Lua 示例：

```lua
m.sleep(1000)
```

返回：

```lua
true
```

失败：

```lua
nil, "invalid duration"
```

### 4.3 `m.device.info()`

说明：

- 获取设备基础信息
- 第一版至少返回平台名称和引擎版本

Lua 示例：

```lua
local info = m.device.info()
m.log.print(info.platform)
```

返回示例：

```lua
{
    platform = "android",
    engineVersion = "0.1.0",
    luaVersion = "Lua 5.4",
    rootModeEnabled = true,
    rootAvailable = true,
    accessibilityEnabled = false,
    automationMode = "root-first"
}
```

### 4.3.1 `m.device.isRootAvailable()`

说明：

- 判断当前 Android 设备是否可以通过 `su` 获取 root shell
- Root 模式下引擎启动或切换模式时准备 root 运行层；运行脚本时不重复申请 root，触控和按键只走 root，不改走无障碍，失败时直接返回错误

返回：

```lua
true 或 false
```

### 4.3.2 `m.device.setRootModeEnabled(enabled)` / `m.setRootModeEnabled(...)`

说明：

- 设置 Android Root 模式开关
- 默认开启
- 开启后会准备 root 授权和常驻运行层，触控、按键、输入、截图等自动化能力只走 root
- 关闭后使用无障碍/系统截图授权路线；显式 `m.root.exec(...)` 仍会尝试 root 命令
- 该设置持久化到 App 本地设置，App 界面、脚本和 IDE 查询的是同一份状态

Lua 示例：

```lua
local ok, err = m.device.setRootModeEnabled(true)
if not ok then
    print(err)
end
```

返回：

```lua
true
```

失败：

```lua
nil, "set root mode failed"
```

### 4.3.3 `m.device.screenState/wake/sleep/battery/rotation`

说明：

- 设备状态和设备控制能力，当前 Android 第一版通过 root shell 实现
- 这些接口是显式设备/root 能力，不受 Root 模式开关影响
- `screenState` 返回屏幕亮灭、交互状态、锁屏状态和原始状态字段
- `battery` 返回电量、电源接入、健康状态、电压和温度
- `rotation` 返回自动旋转、锁定状态和当前方向
- `setRotation(rotation, locked)` 支持 `0/1/2/3` 或 `90/180/270`，`locked` 默认 `true`

Lua 示例：

```lua
local state = m.device.screenState()
local battery = m.device.battery()
local rotation = m.device.rotation()

print(state.screenOn, battery.percent, rotation.degrees)
m.device.setRotation(0, true)
```

返回：

```lua
-- screenState
{
    interactive = true,
    screenOn = true,
    locked = false,
    wakefulness = "Awake",
    displayState = "ON"
}

-- battery
{
    percent = 100,
    status = "full",
    plugged = "usb",
    temperatureC = 30.0
}

-- rotation
{
    autoRotate = false,
    locked = true,
    currentRotation = 0,
    degrees = 0
}
```

### 4.3.4 `m.device.settings.*` / `m.device.prop.*`

说明：

- `m.device.settings.get(namespace, key)` 通过 root `settings get` 读取系统设置
- `m.device.settings.put(namespace, key, value)` 通过 root `settings put` 写入系统设置
- `m.device.settings.delete(namespace, key)` 通过 root `settings delete` 删除系统设置
- `m.device.prop.get(key)` 通过 root `getprop` 读取系统属性
- `m.device.prop.set(key, value)` 通过 root `setprop` 写入系统属性
- `namespace` 只支持 `system`、`secure`、`global`

Lua 示例：

```lua
local brightness = m.device.settings.get("system", "screen_brightness")
local model = m.device.prop.get("ro.product.model")
print(brightness, model)
```

### 4.3.5 `m.device.display.*`

说明：

- 通过 root `wm size`、`wm density` 和 `settings` 管理显示参数
- `info()` 返回物理分辨率、覆盖分辨率、有效分辨率、物理 DPI、覆盖 DPI、有效 DPI、亮度和自动亮度状态
- `setSize(width, height)` 设置覆盖分辨率，`resetSize()` 恢复系统默认分辨率
- `setDensity(density)` 设置覆盖 DPI，`resetDensity()` 恢复系统默认 DPI
- `setBrightness(brightness)` 设置亮度，取值 `0..255`，会先关闭自动亮度
- `setAutoBrightness(enabled)` 设置自动亮度开关
- 这些接口是显式 root 能力，不受 Root 模式开关影响

Lua 示例：

```lua
local info = m.device.display.info()
print(info.effectiveWidth, info.effectiveHeight, info.effectiveDensity)

m.device.display.setBrightness(180)
m.device.display.setAutoBrightness(false)
```

返回示例：

```lua
{
    physicalWidth = 1080,
    physicalHeight = 2220,
    effectiveWidth = 1080,
    effectiveHeight = 2220,
    physicalDensity = 440,
    effectiveDensity = 440,
    brightness = 180,
    brightnessMode = 0,
    autoBrightness = false
}
```

### 4.3.6 `m.root.exec(command, timeoutMs)` / `m.rootExec(...)`

说明：

- 通过 root shell 执行命令
- `timeoutMs` 可选，默认 2500，最大 30000
- 返回结构化结果，不丢弃 stdout/stderr
- 显式 root API 不受界面 Root 模式开关影响

Lua 示例：

```lua
local result = m.root.exec("id -u", 2000)
if result.ok then
    print(result.stdout)
else
    print(result.error)
end
```

返回示例：

```lua
{
    ok = true,
    exitCode = 0,
    stdout = "0\n",
    stderr = "",
    timedOut = false,
    error = ""
}
```

### 4.3.7 `m.root.file.*`

说明：

- root 文本文件能力，适合读取和写入普通 UTF-8 文本
- 所有方法都是显式 root 能力，不受 Root 模式开关影响
- `writeText` 覆盖写入，内部通过 base64 传输文本，减少 shell 转义问题
- `stat/list` 返回结构化文件信息，`mkdir` 默认递归创建目录，`chmod/chown` 修改权限和属主
- 第一版不承诺二进制文件传输

函数：

| 函数 | 返回 |
|---|---|
| `m.root.file.exists(path)` | `true` 或 `false` |
| `m.root.file.readText(path, timeoutMs)` | 成功返回文本，失败返回 `nil, errorMessage` |
| `m.root.file.writeText(path, content, timeoutMs)` | 成功返回 `true`，失败返回 `nil, errorMessage` |
| `m.root.file.stat(path)` | 成功返回文件信息表，失败返回 `nil, errorMessage` |
| `m.root.file.list(path)` | 成功返回文件信息表数组，失败返回 `nil, errorMessage` |
| `m.root.file.remove(path, recursive)` | 成功返回 `true`，失败返回 `nil, errorMessage` |
| `m.root.file.mkdir(path, recursive)` | 成功返回 `true`，失败返回 `nil, errorMessage` |
| `m.root.file.chmod(path, mode)` | 成功返回 `true`，失败返回 `nil, errorMessage` |
| `m.root.file.chown(path, owner)` | 成功返回 `true`，失败返回 `nil, errorMessage` |

Lua 示例：

```lua
local path = "/data/local/tmp/demo.txt"
local ok, err = m.root.file.writeText(path, "hello\n中文")
if not ok then
    print(err)
    return
end

local text = m.root.file.readText(path)
print(text)
local info = m.root.file.stat(path)
print(info.name, info.size, info.mode)
m.root.file.chmod(path, "644")
m.root.file.chown(path, "root:shell")
m.root.file.remove(path)
```

### 4.3.8 `m.root.process.*`

说明：

- root 进程能力，提供 PID 查询、进程列表、进程详情、资源统计和结束进程
- 所有方法都是显式 root 能力，不受 Root 模式开关影响
- `pidOf` 使用 Android 系统 `pidof`，找不到进程时返回空表
- `list/info` 使用 Android 系统 `ps`，返回结构化进程表
- `stats` 读取 `/proc/<pid>/status`，返回单个进程的内存、线程和 UID/GID 等字段
- `kill` 支持 PID 或进程名，默认信号为 `15`

函数：

| 函数 | 返回 |
|---|---|
| `m.root.process.pidOf(name)` | 成功返回 PID 数组，失败返回 `nil, errorMessage` |
| `m.root.process.list()` | 成功返回进程对象数组，失败返回 `nil, errorMessage` |
| `m.root.process.info(target)` | 成功返回进程对象数组，失败返回 `nil, errorMessage` |
| `m.root.process.stats(target)` | 成功返回进程资源对象，失败返回 `nil, errorMessage` |
| `m.root.process.kill(target, signal)` | 成功返回 `true`，失败返回 `nil, errorMessage` |

Lua 示例：

```lua
local pids, err = m.root.process.pidOf("com.example.target")
if not pids then
    print(err)
    return
end

if #pids > 0 then
    m.root.process.kill(pids[1], 15)
end

local processes, processErr = m.root.process.list()
if processes then
    for i, process in ipairs(processes) do
        print(process.pid, process.user, process.name, process.args)
    end
else
    print(processErr)
end

local stats, statsErr = m.root.process.stats("com.example.target")
if stats then
    print(stats.pid, stats.name, stats.vmRssKb, stats.threads)
else
    print(statsErr)
end
```

### 4.4 `m.app.isInstalled(packageName)` / `m.isAppInstalled(...)`

说明：

- 判断 Android 包是否已安装
- 包名无效时返回 false

Lua 示例：

```lua
if m.app.isInstalled("com.android.settings") then
    print("installed")
end
```

返回：

```lua
true 或 false
```

### 4.5 `m.app.open(packageName)` / `m.app.start(packageName)` / `m.openApp(...)`

说明：

- 启动 Android 应用
- Root 模式下只使用 root `monkey`
- 无障碍优先模式下才使用普通 Launcher Intent

Lua 示例：

```lua
local ok, err = m.app.open("com.android.settings")
if not ok then
    print(err)
end
```

### 4.6 `m.app.stop(packageName)` / `m.stopApp(...)`

说明：

- 强停 Android 应用
- 当前通过 root `am force-stop` 实现
- root 不可用时返回失败

Lua 示例：

```lua
local ok, err = m.app.stop("com.example.target")
if not ok then
    print(err)
end
```

### 4.7 `m.app.clearData(packageName)` / `m.clearAppData(...)`

说明：

- 清理指定 Android 应用数据
- 当前通过 root `pm clear` 实现
- root 不可用或包名无效时返回失败

Lua 示例：

```lua
local ok, err = m.app.clearData("com.example.target")
if not ok then
    print(err)
end
```

### 4.8 `m.app.grant(packageName, permission)` / `m.grantAppPermission(...)`

说明：

- 给指定 Android 应用授予权限
- 当前通过 root `pm grant` 实现
- root 不可用、包名无效、权限名无效或目标权限不可授予时返回失败

Lua 示例：

```lua
local ok, err = m.app.grant("com.example.target", "android.permission.CAMERA")
if not ok then
    print(err)
end
```

### 4.9 `m.app.revoke(packageName, permission)` / `m.revokeAppPermission(...)`

说明：

- 撤销指定 Android 应用权限
- 当前通过 root `pm revoke` 实现

Lua 示例：

```lua
local ok, err = m.app.revoke("com.example.target", "android.permission.CAMERA")
if not ok then
    print(err)
end
```

### 4.10 `m.app.current()` / `m.currentApp()`

说明：

- 获取当前前台应用
- 当前通过 root `dumpsys window` 实现
- root 不可用或系统没有可解析的前台窗口时返回失败

Lua 示例：

```lua
local current, err = m.app.current()
if current then
    print(current.packageName, current.activityName)
else
    print(err)
end
```

### 4.11 `m.app.install(apkPath, replace)` / `m.installApp(...)`

说明：

- 安装 Android APK
- 当前通过 root `pm install` 实现
- `replace` 默认 `true`，对应 `pm install -r`
- root 不可用、APK 路径无效或安装失败时返回失败

Lua 示例：

```lua
local ok, err = m.app.install("/sdcard/Download/demo.apk")
if not ok then
    print(err)
end
```

### 4.12 `m.app.uninstall(packageName, keepData)` / `m.uninstallApp(...)`

说明：

- 卸载指定 Android 应用
- 当前通过 root `pm uninstall` 实现
- `keepData` 默认 `false`；为 `true` 时使用 `pm uninstall -k`

Lua 示例：

```lua
local ok, err = m.app.uninstall("com.example.target")
if not ok then
    print(err)
end
```

### 4.13 `m.app.disable(packageName)` / `m.disableApp(...)`

说明：

- 冻结指定 Android 应用
- 当前通过 root `pm disable-user --user 0` 实现

Lua 示例：

```lua
local ok, err = m.app.disable("com.example.target")
if not ok then
    print(err)
end
```

### 4.14 `m.app.enable(packageName)` / `m.enableApp(...)`

说明：

- 解冻指定 Android 应用
- 当前通过 root `pm enable` 实现

Lua 示例：

```lua
local ok, err = m.app.enable("com.example.target")
if not ok then
    print(err)
end
```

### 4.15 `m.app.disableComponent(component)` / `m.disableAppComponent(...)`

说明：

- 冻结指定 Android 组件
- 当前通过 root `pm disable-user --user 0 package/component` 实现
- 组件名必须是 `package/class` 形式，例如 `com.example.target/.MainActivity`

Lua 示例：

```lua
local ok, err = m.app.disableComponent("com.example.target/.MainActivity")
if not ok then
    print(err)
end
```

### 4.16 `m.app.enableComponent(component)` / `m.enableAppComponent(...)`

说明：

- 解冻指定 Android 组件
- 当前通过 root `pm enable package/component` 实现

Lua 示例：

```lua
local ok, err = m.app.enableComponent("com.example.target/.MainActivity")
if not ok then
    print(err)
end
```

### 4.17 `m.file.read(path)`

说明：

- 读取文本文件
- 第一版只支持 UTF-8 文本

Lua 示例：

```lua
local text, err = m.file.read("/sdcard/test.txt")
if not text then
    m.log.print(err)
end
```

成功：

```lua
"file content"
```

失败：

```lua
nil, "message"
```

当前 Android 第一版错误消息：

```lua
nil, "open file failed"
```

### 4.16 `m.file.write(path, content)`

说明：

- 写入文本文件
- 第一版覆盖写入

Lua 示例：

```lua
local ok, err = m.file.write("/sdcard/test.txt", "hello")
```

成功：

```lua
true
```

失败：

```lua
nil, "message"
```

### 4.17 `m.file.exists(path)`

说明：

- 判断文件是否存在
- 第一版只做普通文件存在性判断；目录能力后续按需要再补

Lua 示例：

```lua
if m.file.exists(path) then
    print("exists")
end
```

返回：

```lua
true 或 false
```

### 4.18 `m.file.remove(path)`

说明：

- 删除文件
- 删除不存在或无权限时返回错误

Lua 示例：

```lua
local ok, err = m.file.remove(path)
if not ok then
    print(err)
end
```

成功：

```lua
true
```

失败：

```lua
nil, "message"
```

### 4.19 `m.file.appDataPath(fileName)`

说明：

- 拼出当前 App 私有文件目录下的路径
- 适合保存脚本临时文件和调试输出，避免第一版外部存储权限问题

Lua 示例：

```lua
local path = m.file.appDataPath("demo.txt")
local ok, err = m.file.write(path, "hello")
if not ok then
    print(err)
end
```

返回：

```lua
"/data/user/0/com.autolua.engine/files/demo.txt"
```

## 5. HostApi v0.2 预留

### 5.0 `script.stop`

说明：

- 当前 Java 测试页已支持 Stop 按钮
- Native 层通过 Lua debug hook 做协作取消
- 不强杀线程

当前验证：

```text
Lua run failed: script stopped
```

### 5.0.1 `script.pause` / `script.resume`

说明：

- `script.pause` 请求暂停当前脚本
- `script.resume` 请求继续已暂停脚本
- 暂停通过 Lua debug hook 协作执行，不直接挂起系统线程
- 如果脚本正在执行很长的宿主函数，暂停会等宿主函数返回 Lua VM 后生效

状态：

```text
running -> pausing -> paused -> running
```

### 5.1 `m.touch.tap(x, y)` / `m.tap(x, y)`

说明：

- 点击屏幕坐标
- Root 模式下只走 root `input tap`；无障碍优先模式下才走无障碍点击
- 如果 root 和无障碍都不可用，返回 `nil, "touch tap failed; root or accessibility service is not available"`

Lua 示例：

```lua
local ok, err = m.touch.tap(300, 500)
if not ok then
    print(err)
end
```

### 5.2 `m.touch.swipe(x1, y1, x2, y2, duration)` / `m.swipe(...)`

说明：

- 滑动屏幕
- `duration` 单位毫秒
- Root 模式下只走 root `input swipe`；无障碍优先模式下才走无障碍滑动
- 如果 root 和无障碍都不可用，返回 `nil, "touch swipe failed; root or accessibility service is not available"`

Lua 示例：

```lua
local ok, err = m.touch.swipe(300, 800, 300, 300, 500)
if not ok then
    print(err)
end
```

### 5.3 `m.input.text(text)` / `m.inputText(text)`

说明：

- 向当前焦点输入框输入文本
- Android 第一版只使用 root `input text`
- 中文、换行和复杂符号由脚本显式调用 `m.input.pasteText`
- Root 模式关闭、root 不可用或当前焦点控件无法接收输入时返回 `nil, "input text failed; root is not available or focused control cannot receive text"`

Lua 示例：

```lua
local ok, err = m.input.text("hello world")
if not ok then
    print(err)
end
```

### 5.4 `m.input.pasteText(text)` / `m.pasteText(text)`

说明：

- 通过剪贴板向当前焦点控件粘贴文本
- 适合中文、换行和复杂符号
- 该接口会覆盖系统剪贴板内容
- 依赖 root `KEYCODE_PASTE` 和当前焦点控件的粘贴能力

Lua 示例：

```lua
local ok, err = m.input.pasteText("中文输入\n第二行")
if not ok then
    print(err)
end
```

### 5.5 `m.key.isAccessibilityEnabled()`

说明：

- 返回当前无障碍服务是否已开启
- 这是只读状态检查，不会触发任何系统操作

Lua 示例：

```lua
if not m.key.isAccessibilityEnabled() then
    print("accessibility service is not enabled")
end
```

返回：

```lua
true 或 false
```

### 5.6 `m.key.press(keyCode)` / `m.pressKey(keyCode)`

说明：

- 执行 Android 通用按键码
- Root 模式下只使用 root `input keyevent`
- 无障碍优先模式下 `keyCode = 4` 和 `keyCode = 3` 走无障碍返回/Home
- 其他 keyCode 当前依赖 root；无 root 时返回 `nil, "key press failed; root or accessibility service is not available"`

Lua 示例：

```lua
local ok, err = m.key.press(66)
if not ok then
    print(err)
end
```

### 5.7 `m.key.back()` / `m.back()`

说明：

- 执行系统返回键
- Root 模式下只走 root `input keyevent`；无障碍优先模式下走无障碍全局动作
- 如果 root 和无障碍都不可用，返回 `nil, "key back failed; root or accessibility service is not available"`

Lua 示例：

```lua
local ok, err = m.key.back()
if not ok then
    print(err)
end
```

### 5.8 `m.key.home()` / `m.home()`

说明：

- 执行系统主页键
- Root 模式下只走 root `input keyevent`；无障碍优先模式下走无障碍全局动作
- 如果 root 和无障碍都不可用，返回 `nil, "key home failed; root or accessibility service is not available"`

Lua 示例：

```lua
local ok, err = m.key.home()
if not ok then
    print(err)
end
```

### 5.9 `m.screen.capture()` / `m.capture()`

说明：

- 获取当前屏幕截图
- Android 第一版返回内存图片句柄，不做 PNG 编码，不写磁盘
- 句柄只暴露基础元信息；后续找色、比色直接在 native 内存上处理
- 默认 20ms 内连续截图复用当前屏幕帧，脚本可修改缓冲时间
- 缓存未命中时，Root 模式使用 root helper 常驻进程
- 缓存未命中时，无障碍优先模式使用 MediaProjection 系统截图授权
- root 不可用或未授权截图时返回 `nil, errorMessage`

Lua 示例：

```lua
local img, err = m.screen.capture()
if img then
    print(img.id, img.width, img.height, img.format, img.source, img.captureDurationMs)
else
    print(err)
end
```

Android 第一版成功返回：

```lua
{
    id = 1,
    type = "image",
    width = 1080,
    height = 2220,
    rowStride = 4320,
    pixelStride = 4,
    byteLength = 9590400,
    format = "rgba8888",
    source = "root-helper",
    captureDurationMs = 54
}
```

字段说明：

- `source` 当前为 `"root-helper"` 或 `"media-projection"`，用于确认本帧实际截图路线。
- `captureDurationMs` 是 Java 层取到这一帧的耗时，单位毫秒，用于后续 root 截图压测。
- 屏幕帧由引擎管理，默认 20ms 内连续截图复用当前帧，超过缓冲时间重新取屏并覆盖旧帧。
- `m.keepCapture()` 会锁住当前屏幕帧持续复用，`m.releaseCapture()` 恢复按缓冲时间复用，`m.setCaptureCacheMs(ms)` 可修改缓冲时间。

### 5.9.1 屏幕帧生命周期

说明：

- `m.screen.capture()` 返回当前屏幕帧句柄，脚本不需要释放。
- 缓冲时间内连续调用截图 API，直接复用当前屏幕帧句柄；默认缓冲时间是 20ms。
- 超过缓冲时间后再次调用截图 API，重新取屏并覆盖旧屏幕帧。
- `m.keepCapture()` 会始终复用当前屏幕帧；`m.releaseCapture()` 会取消锁帧，恢复按缓冲时间复用。
- `m.setCaptureCacheMs(ms)` 设置当前脚本运行期缓冲时间，`ms` 必须大于或等于 0。
- 脚本结束或新脚本开始时，引擎会清空屏幕帧缓存，并恢复默认 20ms 缓冲时间。

### 5.9.2 `m.image.getPixel(image, x, y)` / `m.getPixel(...)`

说明：

- 从 `m.screen.capture()` 返回的 native 内存图片句柄读取单个像素
- 坐标从 `0` 开始
- 不重新截图、不编码 PNG、不写磁盘
- 当前返回 RGBA 拆分值，方便后续脚本或 native 算法复用

Lua 示例：

```lua
local img = m.screen.capture()
if img then
    local rgb, r, g, b, a = m.image.getPixel(img, 100, 200)
    if rgb then
        print(rgb, r, g, b, a)
    end
end
```

成功：

```lua
rgb, r, g, b, a
```

失败：

```lua
nil, "message"
```

### 5.9.3 `m.image.getPixels(image, points)` / `m.getPixels(...)`

说明：

- 批量读取多个坐标的 RGB 值
- 用于高频点阵读取，减少 Lua 与 native 之间的调用次数
- `points` 支持 `{x1, y1, x2, y2}` 扁平数组，也支持 `{ {x=1,y=2}, {3,4} }`
- 只读取已有图片句柄，不负责找色、比色等算法

Lua 示例：

```lua
local colors = m.image.getPixels(img, {
    { x = 10, y = 10 },
    { 20, 20 },
})
```

成功：

```lua
{ rgb1, rgb2 }
```

失败：

```lua
nil, "message"
```

### 5.10 `m.image.findColor(image, color, x1, y1, x2, y2, tolerance)`

说明：

- 在图片指定区域查找颜色
- `color` 建议使用 `#RRGGBB`
- 当前未实现。后续会基于 `m.screen.capture()` 的 native 内存句柄和 `m.image.getPixels` 的点阵读取能力继续做。

Lua 示例：

```lua
local pos = m.image.findColor(img, "#ff0000", 0, 0, 1080, 2400, 10)
if pos then
    m.tap(pos.x, pos.y)
end
```

## 6. 能力表

| API | Android 第一版 | Windows 后续 | iOS 后续 |
|---|---:|---:|---:|
| `m.log.print` | 支持 | 预留 | 预留 |
| `m.sleep` | 支持 | 预留 | 预留 |
| `m.device.info` | 支持 | 预留 | 预留 |
| `m.device.setRootModeEnabled` | 支持 | 预留 | 受限 |
| `m.device.screenState/wake/sleep/battery/rotation/setRotation` | 支持，root 设备状态和方向控制第一版 | 预留 | 受限 |
| `m.device.settings.*` | 支持，root settings get/put/delete 第一版 | 预留 | 受限 |
| `m.device.prop.*` | 支持，root getprop/setprop 第一版 | 预留 | 受限 |
| `m.device.display.*` | 支持，root 显示参数和亮度控制第一版 | 预留 | 受限 |
| `m.root.exec` | 支持 | 预留 | 受限 |
| `m.root.file.*` | 支持，文本、stat/list、mkdir、chmod/chown、递归删除第一版 | 预留 | 受限 |
| `m.root.process.*` | 支持，pidOf/list/info/stats/kill 第一版 | 预留 | 受限 |
| `m.app.isInstalled` | 支持 | 预留 | 受限 |
| `m.app.open` | 支持，Root 模式 root-only / 无障碍优先模式 Intent | 预留 | 受限 |
| `m.app.stop` | 支持，root | 预留 | 受限 |
| `m.app.clearData` | 支持，root | 预留 | 受限 |
| `m.app.grant` | 支持，root | 预留 | 受限 |
| `m.app.revoke` | 支持，root | 预留 | 受限 |
| `m.app.current` | 支持，root | 预留 | 受限 |
| `m.app.install` | 支持，root | 预留 | 受限 |
| `m.app.uninstall` | 支持，root | 预留 | 受限 |
| `m.app.disable` | 支持，root | 预留 | 受限 |
| `m.app.enable` | 支持，root | 预留 | 受限 |
| `m.app.disableComponent` | 支持，root | 预留 | 受限 |
| `m.app.enableComponent` | 支持，root | 预留 | 受限 |
| `m.file.read` | 支持 | 预留 | 预留 |
| `m.file.write` | 支持 | 预留 | 预留 |
| `m.file.exists` | 支持 | 预留 | 预留 |
| `m.file.remove` | 支持 | 预留 | 预留 |
| `m.file.appDataPath` | 支持 | 预留 | 预留 |
| `m.touch.tap` | 支持，Root 模式 root-only / 无障碍优先模式无障碍 | 预留 | 受限 |
| `m.touch.swipe` | 支持，Root 模式 root-only / 无障碍优先模式无障碍 | 预留 | 受限 |
| `m.input.text` | 支持，root input text，只走 root | 预留 | 受限 |
| `m.input.pasteText` | 支持，root 剪贴板粘贴 | 预留 | 受限 |
| `m.key.isAccessibilityEnabled` | 支持 | 预留 | 预留 |
| `m.device.isRootAvailable` | 支持 | 预留 | 受限 |
| `m.key.press` | 支持，Root 模式 root-only / 无障碍优先模式 Back、Home | 预留 | 受限 |
| `m.key.back` | 支持，Root 模式 root-only / 无障碍优先模式无障碍 | 预留 | 受限 |
| `m.key.home` | 支持，Root 模式 root-only / 无障碍优先模式无障碍 | 预留 | 受限 |
| `m.screen.capture` | 支持，Root 模式 root-only / 无障碍优先模式 MediaProjection | 预留 | 受限 |
| `m.root.screen.capture` | 支持，缓存未命中时显式 root 截图 | 预留 | 受限 |
| `m.screen.keepCapture` | 支持 | 预留 | 预留 |
| `m.screen.releaseCapture` | 支持 | 预留 | 预留 |
| `m.screen.setCaptureCacheMs` | 支持 | 预留 | 预留 |
| `m.image.getPixel` | 支持 | 预留 | 预留 |
| `m.image.getPixels` | 支持 | 预留 | 预留 |
| `m.image.findColor` | 未实现 | 预留 | 预留 |

## 7. 文档维护规则

每新增一个脚本 API，必须同步更新：

1. API 说明
2. 参数表
3. 返回值
4. Lua 示例
5. 平台能力表
6. `docs/脚本文档.md`
