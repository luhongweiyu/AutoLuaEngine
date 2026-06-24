# AutoLuaEngine 脚本文档

本文档面向脚本编写者，记录当前 Android + Lua 引擎已经可以直接调用的脚本 API。

参考了懒人精灵、触动精灵这类自动化工具的文档组织方式：先说明运行环境，再按日志、设备、文件、触控、按键、截图、图片句柄分类。本文档详细说明 AutoLuaEngine 自己的 `m.*` API；懒人精灵、触动精灵兼容层只记录当前兼容状态，不重复第三方文档的完整用法。

## 已完成功能速查

### 正式 API

| 分类 | 函数 | 快捷别名 | 详情 |
|---|---|---|---|
| 全局 | `print(...)` | 无 | [查看](#api-print) |
| 命名空间 | `useApi(name)` | `switchApi(name)` | [查看](#api-useapi) |
| 延时 | `m.sleep(ms)` | 兼容层可导出 `sleep(ms)` | [查看](#api-sleep) |
| 日志 | `m.log.print(text)` | 无 | [查看](#api-log-print) |
| 设备 | `m.device.info()` | 无 | [查看](#api-device-info) |
| 设备 | `m.device.isRootAvailable()` | `m.isRootAvailable()` | [查看](#api-device-root) |
| 设备 | `m.device.setRootModeEnabled(enabled)` | `m.setRootModeEnabled(...)` | [查看](#api-device-root-mode) |
| Root | `m.root.exec(command, timeoutMs)` | `m.rootExec(...)` | [查看](#api-root-exec) |
| Root 文件 | `m.root.file.exists(path)` | `m.rootFileExists(path)` | [查看](#api-root-file-exists) |
| Root 文件 | `m.root.file.readText(path, timeoutMs)` | `m.rootReadText(...)` | [查看](#api-root-file-read-text) |
| Root 文件 | `m.root.file.writeText(path, content, timeoutMs)` | `m.rootWriteText(...)` | [查看](#api-root-file-write-text) |
| Root 文件 | `m.root.file.remove(path)` | `m.rootRemove(path)` | [查看](#api-root-file-remove) |
| Root 文件 | `m.root.file.mkdir(path, recursive)` | `m.rootMkdir(...)` | [查看](#api-root-file-mkdir) |
| Root 文件 | `m.root.file.chmod(path, mode)` | `m.rootChmod(...)` | [查看](#api-root-file-chmod) |
| Root 进程 | `m.root.process.pidOf(name)` | `m.rootPidOf(name)` | [查看](#api-root-process-pid-of) |
| Root 进程 | `m.root.process.kill(target, signal)` | `m.rootKill(...)` | [查看](#api-root-process-kill) |
| 应用 | `m.app.isInstalled(packageName)` | `m.isAppInstalled(...)` | [查看](#api-app-installed) |
| 应用 | `m.app.open(packageName)` | `m.openApp(...)` | [查看](#api-app-open) |
| 应用 | `m.app.stop(packageName)` | `m.stopApp(...)` | [查看](#api-app-stop) |
| 应用 | `m.app.clearData(packageName)` | `m.clearAppData(...)` | [查看](#api-app-clear-data) |
| 应用 | `m.app.grant(packageName, permission)` | `m.grantAppPermission(...)` | [查看](#api-app-grant) |
| 应用 | `m.app.revoke(packageName, permission)` | `m.revokeAppPermission(...)` | [查看](#api-app-revoke) |
| 文件 | `m.file.appDataPath(fileName)` | 无 | [查看](#api-file-app-data-path) |
| 文件 | `m.file.write(path, content)` | 无 | [查看](#api-file-write) |
| 文件 | `m.file.read(path)` | 无 | [查看](#api-file-read) |
| 文件 | `m.file.exists(path)` | 无 | [查看](#api-file-exists) |
| 文件 | `m.file.remove(path)` | 无 | [查看](#api-file-remove) |
| 触控 | `m.touch.tap(x, y)` | `m.tap(x, y)` | [查看](#api-touch-tap) |
| 触控 | `m.touch.swipe(x1, y1, x2, y2, duration)` | `m.swipe(...)` | [查看](#api-touch-swipe) |
| 输入 | `m.input.text(text)` | `m.inputText(text)` | [查看](#api-input-text) |
| 输入 | `m.input.pasteText(text)` | `m.pasteText(text)` | [查看](#api-input-paste-text) |
| 按键 | `m.key.isAccessibilityEnabled()` | 无 | [查看](#api-key-accessibility) |
| 按键 | `m.key.press(keyCode)` | `m.pressKey(keyCode)` | [查看](#api-key-press) |
| 按键 | `m.key.back()` | `m.back()` | [查看](#api-key-back) |
| 按键 | `m.key.home()` | `m.home()` | [查看](#api-key-home) |
| 截图 | `m.screen.capture()` | `m.capture()` | [查看](#api-screen-capture) |
| 图片 | `m.image.release(image)` | `m.releaseImage(image)` | [查看](#api-image-release) |
| 图片 | `m.image.getPixel(image, x, y)` | `m.getPixel(...)` | [查看](#api-image-get-pixel) |
| 图片 | `m.image.getPixels(image, points)` | `m.getPixels(...)` | [查看](#api-image-get-pixels) |

### 兼容入口

`lr` 和 `cd` 当前只映射已经由底层实现的基础能力，完整兼容后续按懒人精灵、触动精灵文档逐项补齐。

| 命名空间 | 已有函数 |
|---|---|
| `lr` | `print`、`sleep`、`tap`、`swipe`、`inputText`、`pasteText`、`pressKey`、`back`、`home`、`isRootAvailable`、`rootExec`、`runApp`、`closeApp`、`clearAppData`、`grantAppPermission`、`revokeAppPermission`、`isAppInstalled`、`capture`、`getPixel`、`getPixels`、`releaseImage` |
| `cd` | `print`、`sleep`、`tap`、`swipe`、`inputText`、`pasteText`、`pressKey`、`back`、`home`、`isRootAvailable`、`rootExec`、`runApp`、`closeApp`、`clearAppData`、`grantAppPermission`、`revokeAppPermission`、`isAppInstalled`、`capture`、`getPixel`、`getPixels`、`releaseImage` |

## 1. 适用范围

当前版本：

```text
平台：Android
脚本语言：Lua 5.4.8
引擎版本：0.1.0
通讯方式：ADB forward + HTTP JSON-RPC
```

引擎已支持 UTF-8 中文标识符，可以写中文变量名、函数名和字段名：

```lua
local 中文变量 = "正常"
local function 中文函数()
    return 中文变量
end

_G.中文字段 = 123
print(_G["中文" .. "字段"])
```

当前脚本在 Android 引擎内运行。VS Code 插件或 PC 工具负责把脚本文本发送到 App，App 内部通过 native 引擎执行 Lua。

当前不支持：

- 完整兼容懒人精灵或触动精灵全部函数
- FFI 库
- Java 扩展方法
- `newThread`
- JavaScript 脚本
- Go 脚本
- Windows / iOS 引擎
- OCR / YOLO
- 找图、找色算法

这些能力后续可以逐步增加，但第一版先保证 Android + Lua 基础闭环稳定。

## 2. 快速开始

最小脚本：

```lua
print("hello autolua")
m.log.print("log channel works")
m.sleep(500)

local info = m.device.info()
print(info.platform, info.engineVersion, info.luaVersion)
```

从 VS Code 插件运行：

1. 打开脚本所在文件夹。
2. 打开 `.lua` 文件。
3. 点击底部状态栏 `Run Lua`。
4. 点击 `Logs` 读取脚本输出。

从 PC 工具运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\android\run_lua_http.ps1 -FilePath .\my_script.lua -ShowLogs
```

运行前需要 Android App 已启动，电脑端默认通过：

```powershell
adb forward tcp:18380 tcp:18380
```

连接手机内的引擎服务。

## 3. 通用规则

<a id="api-useapi"></a>

### 3.1 API 命名空间

当前 Lua 层有三套入口：

| 命名空间 | 说明 | 当前状态 |
|---|---|---|
| `m` | AutoLuaEngine 自己的正式 API | 已启用 |
| `lr` | 懒人精灵兼容层 | 部分兼容 |
| `cd` | 触动精灵兼容层 | 部分兼容 |

推荐新脚本全部使用 `m.*`：

```lua
m.tap(300, 500)
m.sleep(1000)
local info = m.device.info()
```

兼容旧脚本时，可以显式切换：

```lua
useApi("lr")
tap(300, 500)

useApi("cd")
tap(300, 500)

useApi("m")
tap(300, 500)
```

`useApi(name)` 会把指定命名空间的一层函数导出到 `_G`，不会把所有模块长期默认暴露到全局，避免函数太多太乱。

支持的别名：

| 调用 | 等价 |
|---|---|
| `useApi("m")` | `useApi("mine")` / `useApi("default")` |
| `useApi("lr")` | `useApi("lazy")` |
| `useApi("cd")` | `useApi("touchsprite")` |

底层实现位置：

```text
C++：只注册 native _host
Lua：assets/runtime/api_m.lua
Lua：assets/runtime/compat_lr.lua
Lua：assets/runtime/compat_cd.lua
Lua：assets/runtime/bootstrap.lua
```

### 3.2 返回值规则

Lua API 统一使用下面的返回方式：

```text
成功且有结果：返回实际结果
成功但无结果：返回 true
失败：返回 nil, errorMessage
```

推荐写法：

```lua
local ok, err = m.file.write(m.file.appDataPath("demo.txt"), "hello")
if not ok then
    print("write failed:", err)
    return
end
```

### 3.3 坐标规则

触控坐标和图片坐标都使用屏幕像素坐标。

```text
左上角：0, 0
x 方向：向右增加
y 方向：向下增加
```

截图返回的图片对象中有 `width` 和 `height`，脚本应优先用它们计算坐标，避免写死某台设备的分辨率。

### 3.4 权限规则

触控和按键当前优先使用 Android root `input` 命令，root 不可用或命令失败时回退无障碍服务。脚本可以按需检查当前自动化状态：

```lua
local info = m.device.info()
if info.automationMode == "none" then
    print("需要开启 root 或无障碍服务")
    return
end
```

截图优先使用 root 原始 `screencap`，root 不可用或 root 截图失败时回退 Android MediaProjection 授权。无 root 时，需要先在 App 中点击 `开启截图授权`，并在系统弹窗中确认。

### 3.5 高频截图和点阵读取规则

`m.screen.capture()` 返回的是 native 内存图片句柄，不是 PNG 路径，也不会写磁盘。

高频读取像素时，推荐流程：

```lua
local img, err = m.screen.capture()
if not img then
    print(err)
    return
end

local colors = m.image.getPixels(img, {
    { x = 10, y = 10 },
    { x = 20, y = 20 },
})

m.image.release(img)
```

不要在循环中反复把截图编码成文件再读取。后续找色、比色算法也会基于 native 内存图片句柄实现。

## 4. 全局函数

<a id="api-print"></a>

### 4.1 `print(...)`

输出调试日志。支持多个参数，参数之间用制表符分隔。

参数：

| 名称 | 类型 | 说明 |
|---|---|---|
| `...` | 任意 | 要输出的内容 |

返回值：

```text
无
```

示例：

```lua
print("hello", 123, true)
```

说明：

- 输出会进入 Android Logcat。
- 输出也会进入引擎日志缓冲，IDE/PC 可通过 `log.drain` 读取。

<a id="api-sleep"></a>

### 4.2 `m.sleep(ms)`

暂停当前脚本。

参数：

| 名称 | 类型 | 说明 |
|---|---|---|
| `ms` | number | 暂停毫秒数，必须大于或等于 0 |

返回值：

```lua
true
```

示例：

```lua
print("before")
m.sleep(1000)
print("after")
```

注意：

- 当前会阻塞正在执行的脚本任务。
- 需要可停止的长循环时，应拆成短 `sleep`，避免脚本长时间占用执行权。

## 5. 日志 API

<a id="api-log-print"></a>

### 5.1 `m.log.print(text)`

输出一条日志。

参数：

| 名称 | 类型 | 说明 |
|---|---|---|
| `text` | string | 日志文本 |

返回值：

```lua
true
```

示例：

```lua
local ok, err = m.log.print("script started")
if not ok then
    print(err)
end
```

说明：

- `m.log.print` 当前与 `print` 使用同一条日志通道。
- 后续如果增加日志级别，会优先扩展 `log` 模块。

## 6. 设备 API

<a id="api-device-info"></a>

### 6.1 `m.device.info()`

获取当前引擎和设备基础信息。

参数：

```text
无
```

返回值：

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

示例：

```lua
local info = m.device.info()
print("platform =", info.platform)
print("engine =", info.engineVersion)
print("lua =", info.luaVersion)
print("root mode =", info.rootModeEnabled)
print("root =", info.rootAvailable)
print("accessibility =", info.accessibilityEnabled)
print("mode =", info.automationMode)
```

说明：

- Lua 内部调用当前返回 `platform`、`engineVersion`、`luaVersion`、`rootModeEnabled`、`rootAvailable`、`accessibilityEnabled`、`automationMode`。
- `automationMode` 当前为 `root-first`、`accessibility` 或 `none`。
- `rootModeEnabled` 表示 App 当前是否启用 Root 模式；默认开启。关闭后触控、按键、截图不再优先走 root。
- PC/IDE 通过 JSON-RPC 调用 `device.info` 时，还会包含 `apiLevel`、`packageName`、`httpPort` 等 Android 端信息。

<a id="api-device-root"></a>

### 6.2 `m.device.isRootAvailable()` / `m.isRootAvailable()`

判断当前设备是否可以通过 `su` 获取 root shell。

返回值：

```lua
true 或 false
```

示例：

```lua
if m.device.isRootAvailable() then
    print("root 可用，触控会优先走 root input")
else
    print("root 不可用，会回退无障碍")
end
```

<a id="api-device-root-mode"></a>

### 6.3 `m.device.setRootModeEnabled(enabled)` / `m.setRootModeEnabled(enabled)`

设置当前 App 的 Root 模式开关。

参数：

| 名称 | 类型 | 说明 |
|---|---|---|
| `enabled` | boolean | `true` 表示启用 Root 优先；`false` 表示不主动走 root，触控和按键尽量走无障碍 fallback |

返回值：

```text
成功：true
失败：nil, errorMessage
```

示例：

```lua
local ok, err = m.device.setRootModeEnabled(true)
if not ok then
    print("set root mode failed:", err)
end
```

说明：

- 默认值为 `true`，和 App 主界面“Root 模式：开启”一致。
- 该设置会持久化，App 按钮、脚本和 IDE 查询到的是同一份状态。
- 显式 `m.root.exec(...)` 不受这个开关影响，它始终表示“尝试执行 root 命令”。

<a id="api-root-exec"></a>

### 6.4 `m.root.exec(command, timeoutMs)` / `m.rootExec(...)`

通过 root shell 执行一条命令，返回结构化结果。这个接口是 root 文件、进程、系统命令能力的基础通道。

参数：

| 名称 | 类型 | 说明 |
|---|---|---|
| `command` | string | 要执行的 shell 命令 |
| `timeoutMs` | number | 可选，超时时间，默认 2500，最大 30000 |

返回值：

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

示例：

```lua
local result = m.root.exec("id -u", 2000)
if result.ok then
    print("uid =", result.stdout)
else
    print("root exec failed:", result.error, result.stderr)
end
```

说明：

- `ok` 表示命令退出码是否为 0。
- `exitCode` 非 0 时，命令已经执行完成，但命令本身失败。
- `error` 用于 root 不可用、超时、通道失败这类错误。
- 这个显式 root API 不受界面 Root 模式开关影响；开关只影响触控、按键、截图的自动路由。

<a id="api-root-file-exists"></a>

### 6.5 `m.root.file.exists(path)` / `m.rootFileExists(path)`

通过 root shell 判断路径是否存在。

参数：

| 名称 | 类型 | 说明 |
|---|---|---|
| `path` | string | Android 设备上的绝对路径 |

返回值：

```lua
true 或 false
```

示例：

```lua
if m.root.file.exists("/data/local/tmp/demo.txt") then
    print("root file exists")
end
```

说明：

- 该接口不受 Root 模式开关影响，会直接尝试 root。
- root 不可用、路径不存在、无权限都会返回 `false`；需要错误详情时用 `m.root.exec`。

<a id="api-root-file-read-text"></a>

### 6.6 `m.root.file.readText(path, timeoutMs)` / `m.rootReadText(...)`

通过 root shell 读取 UTF-8 文本文件。

参数：

| 名称 | 类型 | 说明 |
|---|---|---|
| `path` | string | Android 设备上的绝对路径 |
| `timeoutMs` | number | 可选，超时时间，默认 2500，最大 30000 |

返回值：

```text
成功：content
失败：nil, errorMessage
```

示例：

```lua
local text, err = m.root.file.readText("/data/local/tmp/demo.txt")
if not text then
    print("read failed:", err)
end
```

<a id="api-root-file-write-text"></a>

### 6.7 `m.root.file.writeText(path, content, timeoutMs)` / `m.rootWriteText(...)`

通过 root shell 覆盖写入 UTF-8 文本文件。

参数：

| 名称 | 类型 | 说明 |
|---|---|---|
| `path` | string | Android 设备上的绝对路径 |
| `content` | string | 要写入的 UTF-8 文本 |
| `timeoutMs` | number | 可选，超时时间，默认 2500，最大 30000 |

返回值：

```text
成功：true
失败：nil, errorMessage
```

示例：

```lua
local ok, err = m.root.file.writeText("/data/local/tmp/demo.txt", "hello\n中文")
if not ok then
    print("write failed:", err)
end
```

说明：

- 第一版内部用 base64 传输文本，避免空格、换行、引号和中文被 shell 解析破坏。
- 当前只承诺文本文件；二进制文件、目录创建、递归删除后续单独做。

<a id="api-root-file-remove"></a>

### 6.8 `m.root.file.remove(path)` / `m.rootRemove(path)`

通过 root shell 删除文件或路径。

参数：

| 名称 | 类型 | 说明 |
|---|---|---|
| `path` | string | Android 设备上的绝对路径 |

返回值：

```text
成功：true
失败：nil, errorMessage
```

示例：

```lua
local ok, err = m.root.file.remove("/data/local/tmp/demo.txt")
if not ok then
    print("remove failed:", err)
end
```

<a id="api-root-file-mkdir"></a>

### 6.9 `m.root.file.mkdir(path, recursive)` / `m.rootMkdir(...)`

通过 root shell 创建目录。

参数：

| 名称 | 类型 | 说明 |
|---|---|---|
| `path` | string | Android 设备上的绝对路径 |
| `recursive` | boolean | 可选，默认 `true`。为 `true` 时等同 `mkdir -p` |

返回值：

```text
成功：true
失败：nil, errorMessage
```

示例：

```lua
local ok, err = m.root.file.mkdir("/data/local/tmp/autolua", true)
if not ok then
    print("mkdir failed:", err)
end
```

<a id="api-root-file-chmod"></a>

### 6.10 `m.root.file.chmod(path, mode)` / `m.rootChmod(...)`

通过 root shell 修改文件或目录权限。

参数：

| 名称 | 类型 | 说明 |
|---|---|---|
| `path` | string | Android 设备上的绝对路径 |
| `mode` | string | 3 或 4 位八进制权限字符串，例如 `"755"`、`"0644"` |

返回值：

```text
成功：true
失败：nil, errorMessage
```

示例：

```lua
local ok, err = m.root.file.chmod("/data/local/tmp/autolua/run.sh", "755")
if not ok then
    print("chmod failed:", err)
end
```

说明：

- `mode` 必须写成字符串，避免 Lua 数字进制和前导零产生歧义。
- 当前不提供 `chown`，后续按实际脚本部署需求再补。

<a id="api-root-process-pid-of"></a>

### 6.11 `m.root.process.pidOf(name)` / `m.rootPidOf(name)`

通过 root shell 查询进程名对应的 PID 列表。

参数：

| 名称 | 类型 | 说明 |
|---|---|---|
| `name` | string | 进程名，例如包名或 native 进程名 |

返回值：

```text
成功：{ pid1, pid2, ... }
失败：nil, errorMessage
```

示例：

```lua
local pids, err = m.root.process.pidOf("com.android.settings")
if not pids then
    print("pidOf failed:", err)
    return
end

for i, pid in ipairs(pids) do
    print("pid =", pid)
end
```

说明：

- 第一版使用 Android 系统 `pidof`。
- 找不到进程时返回空表，不视为错误。
- 该接口不受 Root 模式开关影响，会直接尝试 root。

<a id="api-root-process-kill"></a>

### 6.12 `m.root.process.kill(target, signal)` / `m.rootKill(...)`

通过 root shell 结束指定进程。

参数：

| 名称 | 类型 | 说明 |
|---|---|---|
| `target` | number 或 string | PID，或进程名 |
| `signal` | number | 可选，默认 `15`。需要强杀时可传 `9` |

返回值：

```text
成功：true
失败：nil, errorMessage
```

示例：

```lua
local ok, err = m.root.process.kill("com.example.target", 15)
if not ok then
    print("kill failed:", err)
end
```

说明：

- 传进程名时会先用 `pidof` 查询 PID，再对查到的 PID 执行 `kill`。
- 找不到进程时返回失败；如果只是想检查是否存在，先用 `m.root.process.pidOf(name)`。

## 7. 应用 API

应用 API 用于启动、停止和检查 Android 包。包名会做基础合法性校验。

<a id="api-app-installed"></a>

### 7.1 `m.app.isInstalled(packageName)` / `m.isAppInstalled(...)`

判断指定包名是否已安装。

```lua
if m.app.isInstalled("com.android.settings") then
    print("settings installed")
end
```

<a id="api-app-open"></a>

### 7.2 `m.app.open(packageName)` / `m.app.start(packageName)` / `m.openApp(...)`

启动指定应用。Root 模式开启且 root 可用时，优先通过 root `monkey` 启动；失败后回退普通 Launcher Intent。

```lua
local ok, err = m.app.open("com.android.settings")
if not ok then
    print("open failed:", err)
end
```

<a id="api-app-stop"></a>

### 7.3 `m.app.stop(packageName)` / `m.stopApp(...)`

强停指定应用。当前通过 root `am force-stop` 实现，root 不可用时会失败。

```lua
local ok, err = m.app.stop("com.example.target")
if not ok then
    print("stop failed:", err)
end
```

<a id="api-app-clear-data"></a>

### 7.4 `m.app.clearData(packageName)` / `m.clearAppData(...)`

清理指定应用数据。当前通过 root `pm clear` 实现，root 不可用时会失败。

参数：

| 名称 | 类型 | 说明 |
|---|---|---|
| `packageName` | string | Android 包名，例如 `"com.example.target"` |

返回值：

```text
成功：true
失败：nil, errorMessage
```

示例：

```lua
local ok, err = m.app.clearData("com.example.target")
if not ok then
    print("clear data failed:", err)
end
```

注意：

- 这个操作会删除目标应用本地数据，脚本里调用前应明确确认目标包名。
- 当前不受 Root 模式开关影响，属于显式 root 应用控制能力。

<a id="api-app-grant"></a>

### 7.5 `m.app.grant(packageName, permission)` / `m.grantAppPermission(...)`

给指定应用授予 Android 权限。当前通过 root `pm grant` 实现，root 不可用或权限不可授予时会失败。

参数：

| 名称 | 类型 | 说明 |
|---|---|---|
| `packageName` | string | Android 包名 |
| `permission` | string | Android 权限名，例如 `"android.permission.CAMERA"` |

返回值：

```text
成功：true
失败：nil, errorMessage
```

示例：

```lua
local ok, err = m.app.grant("com.example.target", "android.permission.CAMERA")
if not ok then
    print("grant failed:", err)
end
```

<a id="api-app-revoke"></a>

### 7.6 `m.app.revoke(packageName, permission)` / `m.revokeAppPermission(...)`

撤销指定应用的 Android 权限。当前通过 root `pm revoke` 实现。

参数：

| 名称 | 类型 | 说明 |
|---|---|---|
| `packageName` | string | Android 包名 |
| `permission` | string | Android 权限名，例如 `"android.permission.CAMERA"` |

返回值：

```text
成功：true
失败：nil, errorMessage
```

示例：

```lua
local ok, err = m.app.revoke("com.example.target", "android.permission.CAMERA")
if not ok then
    print("revoke failed:", err)
end
```

## 8. 文件 API

文件 API 当前只做基础文本读写和存在性判断。第一版建议优先读写 App 私有目录，避免额外申请外部存储权限。

<a id="api-file-app-data-path"></a>

### 8.1 `m.file.appDataPath(fileName)`

拼出 App 私有数据目录下的文件路径。

参数：

| 名称 | 类型 | 说明 |
|---|---|---|
| `fileName` | string | 文件名或相对文件名 |

返回值：

```lua
"/data/user/0/com.autolua.engine/files/demo.txt"
```

示例：

```lua
local path = m.file.appDataPath("demo.txt")
print(path)
```

注意：

- 当前实现会把传入内容直接拼到 App 私有目录后面。
- 建议第一版只传普通文件名，例如 `"demo.txt"`。

<a id="api-file-write"></a>

### 8.2 `m.file.write(path, content)`

写入文本文件。当前是覆盖写入。

参数：

| 名称 | 类型 | 说明 |
|---|---|---|
| `path` | string | 文件路径 |
| `content` | string | 写入内容 |

返回值：

```text
成功：true
失败：nil, errorMessage
```

示例：

```lua
local path = m.file.appDataPath("demo.txt")
local ok, err = m.file.write(path, "hello")
if not ok then
    print("write failed:", err)
end
```

<a id="api-file-read"></a>

### 8.3 `m.file.read(path)`

读取文本文件。

参数：

| 名称 | 类型 | 说明 |
|---|---|---|
| `path` | string | 文件路径 |

返回值：

```text
成功：content
失败：nil, errorMessage
```

示例：

```lua
local path = m.file.appDataPath("demo.txt")
local text, err = m.file.read(path)
if not text then
    print("read failed:", err)
    return
end

print(text)
```

注意：

- 当前按字节读取，不做编码转换。
- 第一版建议写入和读取 UTF-8 文本。

<a id="api-file-exists"></a>

### 8.4 `m.file.exists(path)`

判断文件是否存在。

参数：

| 名称 | 类型 | 说明 |
|---|---|---|
| `path` | string | 文件路径 |

返回值：

```lua
true 或 false
```

示例：

```lua
local path = m.file.appDataPath("demo.txt")
if m.file.exists(path) then
    print("file exists")
end
```

<a id="api-file-remove"></a>

### 8.5 `m.file.remove(path)`

删除文件。

参数：

| 名称 | 类型 | 说明 |
|---|---|---|
| `path` | string | 文件路径 |

返回值：

```text
成功：true
失败：nil, errorMessage
```

示例：

```lua
local path = m.file.appDataPath("demo.txt")
local ok, err = m.file.remove(path)
if not ok then
    print("remove failed:", err)
end
```

## 9. 触控 API

触控 API 当前优先使用 root `input` 命令，失败后回退无障碍服务。脚本调用前可以先检查自动化状态。

```lua
local info = m.device.info()
if info.automationMode == "none" then
    print("root or accessibility service is not available")
    return
end
```

<a id="api-touch-tap"></a>

### 9.1 `m.touch.tap(x, y)` / `m.tap(x, y)`

点击屏幕坐标。Android 端当前优先走 root `input tap`，失败后回退无障碍服务。

参数：

| 名称 | 类型 | 说明 |
|---|---|---|
| `x` | number | 横坐标 |
| `y` | number | 纵坐标 |

返回值：

```text
成功：true
失败：nil, errorMessage
```

示例：

```lua
local ok, err = m.tap(300, 500)
if not ok then
    print("tap failed:", err)
end
```

常见失败：

```text
touch tap failed; root or accessibility service is not available
```

<a id="api-touch-swipe"></a>

### 9.2 `m.touch.swipe(x1, y1, x2, y2, duration)` / `m.swipe(...)`

从一个坐标滑动到另一个坐标。Android 端当前优先走 root `input swipe`，失败后回退无障碍服务。

参数：

| 名称 | 类型 | 说明 |
|---|---|---|
| `x1` | number | 起点横坐标 |
| `y1` | number | 起点纵坐标 |
| `x2` | number | 终点横坐标 |
| `y2` | number | 终点纵坐标 |
| `duration` | number | 滑动耗时，单位毫秒。可省略，默认 300 |

返回值：

```text
成功：true
失败：nil, errorMessage
```

示例：

```lua
local ok, err = m.swipe(500, 1600, 500, 500, 600)
if not ok then
    print("swipe failed:", err)
end
```

## 10. 输入 API

输入 API 当前优先面向 root 设备。`m.input.text` 会先尝试 Android `input text`，失败后自动回退剪贴板粘贴；`m.input.pasteText` 可以显式使用剪贴板路线。

<a id="api-input-text"></a>

### 10.1 `m.input.text(text)` / `m.inputText(text)`

向当前焦点输入框输入文本。

参数：

| 名称 | 类型 | 说明 |
|---|---|---|
| `text` | string | 要输入的文本。简单文本优先走 root `input text`，复杂文本自动回退剪贴板粘贴 |

返回值：

```text
成功：true
失败：nil, errorMessage
```

示例：

```lua
local ok, err = m.input.text("hello world")
if not ok then
    print("input failed:", err)
end
```

当前限制：

- Root 模式关闭或 App 无法获取 root 时会失败。
- 剪贴板回退会覆盖系统当前剪贴板内容。
- 当前焦点控件必须能接收输入或粘贴文本。

常见失败：

```text
input text failed; root is not available or focused control cannot receive text
```

<a id="api-input-paste-text"></a>

### 10.2 `m.input.pasteText(text)` / `m.pasteText(text)`

通过剪贴板和 root 粘贴键向当前焦点输入文本，适合中文、换行和复杂符号。

参数：

| 名称 | 类型 | 说明 |
|---|---|---|
| `text` | string | 要写入剪贴板并粘贴的 UTF-8 文本 |

返回值：

```text
成功：true
失败：nil, errorMessage
```

示例：

```lua
local ok, err = m.input.pasteText("中文输入\n第二行")
if not ok then
    print("paste failed:", err)
end
```

注意：

- 该接口会覆盖系统剪贴板内容。
- 底层会设置剪贴板，然后通过 root `KEYCODE_PASTE` 触发粘贴。
- 当前焦点控件不支持粘贴时会失败或无实际输入效果。

## 11. 按键 API

按键 API 当前优先通过 root `input keyevent` 实现，失败后回退 Android 无障碍服务。

<a id="api-key-accessibility"></a>

### 11.1 `m.key.isAccessibilityEnabled()`

判断当前 App 的无障碍服务是否已开启。

参数：

```text
无
```

返回值：

```lua
true 或 false
```

示例：

```lua
print("accessibility =", m.key.isAccessibilityEnabled())
```

<a id="api-key-press"></a>

### 11.2 `m.key.press(keyCode)` / `m.pressKey(keyCode)`

执行 Android 通用按键码。

参数：

| 名称 | 类型 | 说明 |
|---|---|---|
| `keyCode` | number | Android KeyEvent keyCode，例如 `4` 为返回键，`3` 为 Home 键，`66` 为 Enter |

返回值：

```text
成功：true
失败：nil, errorMessage
```

示例：

```lua
local ok, err = m.key.press(66)
if not ok then
    print("key failed:", err)
end
```

当前路由：

- Root 模式开启且 root 可用时，走 root `input keyevent`。
- `keyCode = 4` 或 `keyCode = 3` 时，root 失败后可回退无障碍返回/Home。
- 其他通用按键当前依赖 root；无 root 时会失败。

常见失败：

```text
key press failed; root or accessibility service is not available
```

<a id="api-key-back"></a>

### 11.3 `m.key.back()` / `m.back()`

执行系统返回键。

参数：

```text
无
```

返回值：

```text
成功：true
失败：nil, errorMessage
```

示例：

```lua
local ok, err = m.back()
if not ok then
    print("back failed:", err)
end
```

<a id="api-key-home"></a>

### 11.4 `m.key.home()` / `m.home()`

执行系统 Home 键。

参数：

```text
无
```

返回值：

```text
成功：true
失败：nil, errorMessage
```

示例：

```lua
local ok, err = m.home()
if not ok then
    print("home failed:", err)
end
```

## 12. 截图 API

<a id="api-screen-capture"></a>

### 12.1 `m.screen.capture()` / `m.capture()`

获取当前屏幕截图，并返回 native 内存图片句柄。Android 端当前优先使用 root 原始 `screencap`，失败后回退 MediaProjection。

参数：

```text
无
```

返回值：

成功：

```lua
{
    id = 1,
    type = "image",
    width = 1080,
    height = 2220,
    rowStride = 4320,
    pixelStride = 4,
    byteLength = 9590400,
    format = "rgba8888"
}
```

失败：

```lua
nil, "screen capture permission is not granted"
```

示例：

```lua
local img, err = m.capture()
if not img then
    print("capture failed:", err)
    return
end

print("image id =", img.id)
print("size =", img.width, img.height)
m.image.release(img)
```

字段说明：

| 字段 | 类型 | 说明 |
|---|---|---|
| `id` | number | native 图片句柄 ID |
| `type` | string | 固定为 `"image"` |
| `width` | number | 图片宽度 |
| `height` | number | 图片高度 |
| `rowStride` | number | 每行实际字节跨度 |
| `pixelStride` | number | 每个像素字节跨度 |
| `byteLength` | number | native 内存像素字节数 |
| `format` | string | 当前为 `"rgba8888"` |

注意：

- 返回的是图片句柄，不是文件路径。
- 图片像素数据保留在 native 内存中。
- 使用完必须调用 `m.image.release(img)`。

## 13. 图片 API

图片 API 当前只处理 `m.screen.capture()` 返回的图片句柄。

<a id="api-image-release"></a>

### 13.1 `m.image.release(image)` / `m.releaseImage(image)`

释放图片句柄。

参数：

| 名称 | 类型 | 说明 |
|---|---|---|
| `image` | table 或 number | `m.screen.capture()` 返回的图片对象，或图片 ID |

返回值：

```text
成功：true
失败：nil, errorMessage
```

示例：

```lua
local img = m.screen.capture()
if img then
    m.image.release(img)
end
```

注意：

- 高频截图脚本必须主动释放旧图片。
- 已释放或不存在的图片句柄会返回错误。

<a id="api-image-get-pixel"></a>

### 13.2 `m.image.getPixel(image, x, y)` / `m.getPixel(...)`

读取图片某个坐标的颜色。

参数：

| 名称 | 类型 | 说明 |
|---|---|---|
| `image` | table 或 number | 图片对象或图片 ID |
| `x` | number | 横坐标，从 0 开始 |
| `y` | number | 纵坐标，从 0 开始 |

返回值：

```text
成功：rgb, r, g, b, a
失败：nil, errorMessage
```

示例：

```lua
local img, err = m.screen.capture()
if not img then
    print(err)
    return
end

local rgb, r, g, b, a = m.image.getPixel(img, 100, 200)
if rgb then
    print("rgb =", rgb)
    print("rgba =", r, g, b, a)
else
    print("getPixel failed:", r)
end

m.image.release(img)
```

说明：

- `rgb` 是整数形式的 RGB 值。
- `r`、`g`、`b`、`a` 是拆分后的通道值。
- 坐标越界会返回错误。

<a id="api-image-get-pixels"></a>

### 13.3 `m.image.getPixels(image, points)` / `m.getPixels(...)`

批量读取多个坐标的 RGB 值。

参数：

| 名称 | 类型 | 说明 |
|---|---|---|
| `image` | table 或 number | 图片对象或图片 ID |
| `points` | table | 坐标列表 |

返回值：

```text
成功：{ rgb1, rgb2, ... }
失败：nil, errorMessage
```

坐标写法一，推荐用于可读性：

```lua
local colors, err = m.image.getPixels(img, {
    { x = 10, y = 10 },
    { x = 20, y = 20 },
})
```

坐标写法二，推荐用于高频场景减少表层级：

```lua
local colors, err = m.image.getPixels(img, {
    10, 10,
    20, 20,
    30, 30,
})
```

完整示例：

```lua
local img, err = m.screen.capture()
if not img then
    print(err)
    return
end

local colors, colorsErr = m.image.getPixels(img, {
    { x = 10, y = 10 },
    { x = 20, y = 20 },
    { 30, 30 },
})

if colors then
    for i, rgb in ipairs(colors) do
        print("color", i, rgb)
    end
else
    print("getPixels failed:", colorsErr)
end

m.image.release(img)
```

说明：

- 只读取已有图片句柄，不会自动截图。
- 批量读取比在 Lua 循环里多次调用 `m.image.getPixel` 更适合高频点阵。
- 当前只返回 RGB 整数，不返回每个点的 RGBA 拆分值。

## 14. 推荐脚本结构

普通自动化脚本建议写成几个小函数，便于后续迁移到 JS 或 Go 时保持 API 语义一致。

```lua
local function requireAutomation()
    local info = m.device.info()
    if info.automationMode ~= "none" then
        return true
    end

    return nil, "root or accessibility service is not available"
end

local function tapCenter()
    local ok, err = requireAutomation()
    if not ok then
        print(err)
        return
    end

    local info = m.device.info()
    print("running on", info.platform)
    m.tap(500, 1000)
end

tapCenter()
```

截图脚本建议固定使用 `pcall` 或手动释放，避免出错后忘记释放图片。

```lua
local img, err = m.screen.capture()
if not img then
    print(err)
    return
end

local ok, runErr = pcall(function()
    local rgb = m.image.getPixel(img, 0, 0)
    print("top-left =", rgb)
end)

m.image.release(img)

if not ok then
    error(runErr)
end
```

## 15. IDE/PC 调用说明

脚本侧只关心 Lua API。IDE/PC 侧通过统一协议控制引擎。

当前已实现的 JSON-RPC 方法：

| 方法 | 说明 |
|---|---|
| `device.info` | 获取设备和引擎信息 |
| `device.setRootModeEnabled` | 设置 Root 模式开关 |
| `script.run` | 发送并执行脚本 |
| `script.pause` | 请求暂停当前脚本 |
| `script.resume` | 请求继续已暂停脚本 |
| `script.stop` | 请求停止当前脚本 |
| `script.status` | 查询脚本状态 |
| `log.drain` | 轮询读取日志 |
| `key.press` | 执行 Android 通用按键码 |
| `input.text` | 向当前焦点输入框输入文本 |
| `input.pasteText` | 通过剪贴板向当前焦点粘贴文本 |
| `app.clearData` | 清理指定 Android 应用数据 |
| `app.grant` | 给指定 Android 应用授予权限 |
| `app.revoke` | 撤销指定 Android 应用权限 |
| `screen.capture` | 从协议侧请求截图句柄 |
| `image.release` | 从协议侧释放图片句柄 |

协议细节见 [Engine Protocol](../shared/protocol/ENGINE_PROTOCOL.md)。

## 16. 与懒人精灵、触动精灵的关系

本文档参考它们的分类方式。当前已经建立 `lr` 和 `cd` 两个兼容命名空间，但只映射已经由底层实现的基础能力，完整兼容需要后续按第三方文档逐项补齐。

兼容层规则：

```lua
lr.tap(300, 500)
cd.tap(300, 500)

useApi("lr")
tap(300, 500)

useApi("cd")
tap(300, 500)
```

兼容层的具体函数用法以第三方原文档为准，我们的文档只记录是否兼容和当前状态。

对应关系大致如下：

| 常见自动化能力 | `m` API | `lr` 兼容 | `cd` 兼容 |
|---|---|---|---|
| 延时 | `m.sleep(ms)` | 部分兼容 | 部分兼容 |
| 点击 | `m.tap(x, y)` | 部分兼容 | 部分兼容 |
| 滑动 | `m.swipe(...)` | 部分兼容 | 部分兼容 |
| 文本输入 | `m.inputText(text)` / `m.pasteText(text)` | 部分兼容 | 部分兼容 |
| 通用按键 | `m.pressKey(keyCode)` | 部分兼容 | 部分兼容 |
| 返回键 | `m.back()` | 部分兼容 | 部分兼容 |
| Home 键 | `m.home()` | 部分兼容 | 部分兼容 |
| 截图 | `m.capture()` | 部分兼容 | 部分兼容 |
| 单点取色 | `m.getPixel(img, x, y)` | 部分兼容 | 部分兼容 |
| 批量取色 | `m.getPixels(img, points)` | 部分兼容 | 部分兼容 |
| 找色 / 比色 | 后续 `m.image.findColor` 等 | 未实现 | 未实现 |
| 控件查找 | 后续 `m.widget` 模块 | 未实现 | 未实现 |
| 应用启动/关闭/数据/权限 | `m.app.open/stop/clearData/grant/revoke` | 部分兼容 | 部分兼容 |
| FFI | 后续评估 | 未实现 | 未实现 |
| 启动线程 | 后续 `m.thread` 模块 | 未实现 | 未实现 |

## 17. 常见错误

### 17.1 `root or accessibility service is not available`

原因：

```text
Android root 不可用，且无障碍服务未开启。
```

处理：

```text
优先确认设备是否允许 App 获取 root；无 root 时，到系统设置里开启 AutoLuaEngine 的无障碍服务。
```

影响 API：

- `m.touch.tap` / `m.tap`
- `m.touch.swipe` / `m.swipe`
- `m.key.press` / `m.pressKey`
- `m.key.back` / `m.back`
- `m.key.home` / `m.home`

### 17.2 `input text failed; root is not available or focused control cannot receive text`

原因：

```text
Root 模式关闭、App 无法获取 root，或当前焦点控件无法接收输入/粘贴文本。
```

处理：

```text
确认 App 主界面 Root 模式为开启，确认设备能给普通 App 授权 su；复杂文本可直接使用 `m.input.pasteText(text)`。
```

影响 API：

- `m.input.text` / `m.inputText`
- `m.input.pasteText` / `m.pasteText`

### 17.3 `screen capture permission is not granted`

原因：

```text
没有完成 Android 截图授权
```

处理：

```text
在 App 中点击 开启截图授权，然后确认系统弹窗
```

影响 API：

- `m.screen.capture` / `m.capture`

### 17.4 `image handle is not found`

原因：

```text
图片 ID 不存在，或已经被释放
```

处理：

```text
确认 m.image.release 不要重复调用，确认传入的是当前 m.screen.capture 返回的图片对象或 ID
```

影响 API：

- `m.image.release` / `m.releaseImage`
- `m.image.getPixel` / `m.getPixel`
- `m.image.getPixels` / `m.getPixels`

### 17.5 `open file failed`

原因：

```text
文件不存在，路径不可访问，或没有权限
```

处理：

```lua
local path = m.file.appDataPath("demo.txt")
m.file.write(path, "hello")
local text, err = m.file.read(path)
```

第一版建议优先使用 `m.file.appDataPath` 得到 App 私有路径。

## 18. 后续文档维护规则

每新增一个脚本 API，必须同步更新本文档：

1. 所属模块
2. 函数签名
3. 参数表
4. 返回值
5. 最小示例
6. 权限要求
7. 常见错误

如果 API 也暴露给 IDE/PC 协议，还需要同步更新 [Engine Protocol](../shared/protocol/ENGINE_PROTOCOL.md)。
