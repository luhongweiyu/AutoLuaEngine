# 统一 API 契约

## 1. 设计目标

统一 API 契约用于约束不同脚本语言、不同平台看到的能力语义。

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
- `m.app.isInstalled`
- `m.app.open`
- `m.app.stop`
- `m.file.read`
- `m.file.exists`
- `m.file.remove`
- `m.screen.capture`
- `m.touch.tap`
- `m.input.text`
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
- `m.root.exec(command, timeoutMs)` / `m.rootExec(...)`
- `m.root.file.exists(path)` / `m.rootFileExists(...)`
- `m.root.file.readText(path, timeoutMs)` / `m.rootReadText(...)`
- `m.root.file.writeText(path, content, timeoutMs)` / `m.rootWriteText(...)`
- `m.root.file.remove(path)` / `m.rootRemove(...)`
- `m.app.isInstalled(packageName)` / `m.isAppInstalled(...)`
- `m.app.open(packageName)` / `m.app.start(packageName)` / `m.openApp(...)`
- `m.app.stop(packageName)` / `m.stopApp(...)`
- `m.file.read(path)`
- `m.file.write(path, content)`
- `m.file.exists(path)`
- `m.file.remove(path)`
- `m.file.appDataPath(fileName)`
- `m.touch.tap(x, y)` / `m.tap(x, y)`
- `m.touch.swipe(x1, y1, x2, y2, duration)` / `m.swipe(...)`
- `m.input.text(text)` / `m.inputText(text)`
- `m.key.isAccessibilityEnabled()`
- `m.key.press(keyCode)` / `m.pressKey(keyCode)`
- `m.key.back()` / `m.back()`
- `m.key.home()` / `m.home()`
- `m.screen.capture()` / `m.capture()`
- `m.image.release(image)` / `m.releaseImage(image)`
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
- 当前触控和按键能力会优先使用 root，失败后回退无障碍

返回：

```lua
true 或 false
```

### 4.3.2 `m.device.setRootModeEnabled(enabled)` / `m.setRootModeEnabled(...)`

说明：

- 设置 Android Root 模式开关
- 默认开启
- 开启后触控、按键、截图等自动化能力优先走 root，再按能力回退无障碍或 MediaProjection
- 关闭后不主动走 root；显式 `m.root.exec(...)` 仍会尝试 root 命令
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

### 4.3.3 `m.root.exec(command, timeoutMs)` / `m.rootExec(...)`

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

### 4.3.4 `m.root.file.*`

说明：

- root 文本文件能力，适合读取和写入普通 UTF-8 文本
- 所有方法都是显式 root 能力，不受 Root 模式开关影响
- `writeText` 覆盖写入，内部通过 base64 传输文本，减少 shell 转义问题
- 第一版不承诺二进制文件、目录创建、递归删除

函数：

| 函数 | 返回 |
|---|---|
| `m.root.file.exists(path)` | `true` 或 `false` |
| `m.root.file.readText(path, timeoutMs)` | 成功返回文本，失败返回 `nil, errorMessage` |
| `m.root.file.writeText(path, content, timeoutMs)` | 成功返回 `true`，失败返回 `nil, errorMessage` |
| `m.root.file.remove(path)` | 成功返回 `true`，失败返回 `nil, errorMessage` |

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
m.root.file.remove(path)
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
- Root 模式开启且 root 可用时，优先使用 root `monkey`
- root 启动失败后回退普通 Launcher Intent

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

### 4.7 `m.file.read(path)`

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

### 4.8 `m.file.write(path, content)`

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

### 4.9 `m.file.exists(path)`

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

### 4.10 `m.file.remove(path)`

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
- Android 优先 root，失败后回退无障碍
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
- Android 优先 root，失败后回退无障碍
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
- Android 第一版使用 root `input text`
- 当前不支持换行；空格会转换为 Android `input text` 使用的 `%s`
- Android `input text` 对中文和复杂特殊字符支持有限，后续再补输入法或剪贴板路线
- Root 模式关闭或 root 不可用时返回 `nil, "input text failed; root is not available or text is unsupported"`

Lua 示例：

```lua
local ok, err = m.input.text("hello world")
if not ok then
    print(err)
end
```

### 5.4 `m.key.isAccessibilityEnabled()`

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

### 5.5 `m.key.press(keyCode)` / `m.pressKey(keyCode)`

说明：

- 执行 Android 通用按键码
- Android 第一版优先使用 root `input keyevent`
- `keyCode = 4` 和 `keyCode = 3` 可在 root 失败后回退无障碍返回/Home
- 其他 keyCode 当前依赖 root；无 root 时返回 `nil, "key press failed; root or accessibility service is not available"`

Lua 示例：

```lua
local ok, err = m.key.press(66)
if not ok then
    print(err)
end
```

### 5.6 `m.key.back()` / `m.back()`

说明：

- 执行系统返回键
- Android 优先 root `input keyevent`，失败后回退无障碍全局动作
- 如果 root 和无障碍都不可用，返回 `nil, "key back failed; root or accessibility service is not available"`

Lua 示例：

```lua
local ok, err = m.key.back()
if not ok then
    print(err)
end
```

### 5.7 `m.key.home()` / `m.home()`

说明：

- 执行系统主页键
- Android 优先 root `input keyevent`，失败后回退无障碍全局动作
- 如果 root 和无障碍都不可用，返回 `nil, "key home failed; root or accessibility service is not available"`

Lua 示例：

```lua
local ok, err = m.key.home()
if not ok then
    print(err)
end
```

### 5.8 `m.screen.capture()` / `m.capture()`

说明：

- 获取当前屏幕截图
- Android 第一版返回内存图片句柄，不做 PNG 编码，不写磁盘
- 句柄只暴露基础元信息；后续找色、比色直接在 native 内存上处理
- Android 优先使用 root 原始 `screencap`，失败后回退 MediaProjection
- root 不可用且未授权时返回 `nil, "screen capture permission is not granted"`

Lua 示例：

```lua
local img, err = m.screen.capture()
if img then
    print(img.id, img.width, img.height, img.format)
    m.image.release(img)
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
    format = "rgba8888"
}
```

### 5.8.1 `m.image.release(image)` / `m.releaseImage(image)`

说明：

- 释放 `m.screen.capture()` 返回的图片句柄
- 高频截图脚本应在用完一帧后主动释放，避免 native 内存累积

Lua 示例：

```lua
local img = m.screen.capture()
if img then
    m.image.release(img)
end
```

### 5.8.2 `m.image.getPixel(image, x, y)` / `m.getPixel(...)`

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
    m.image.release(img)
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

### 5.8.3 `m.image.getPixels(image, points)` / `m.getPixels(...)`

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

### 5.9 `m.image.findColor(image, color, x1, y1, x2, y2, tolerance)`

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
| `m.root.exec` | 支持 | 预留 | 受限 |
| `m.root.file.*` | 支持，文本文件第一版 | 预留 | 受限 |
| `m.app.isInstalled` | 支持 | 预留 | 受限 |
| `m.app.open` | 支持，root 优先 / Intent fallback | 预留 | 受限 |
| `m.app.stop` | 支持，root | 预留 | 受限 |
| `m.file.read` | 支持 | 预留 | 预留 |
| `m.file.write` | 支持 | 预留 | 预留 |
| `m.file.exists` | 支持 | 预留 | 预留 |
| `m.file.remove` | 支持 | 预留 | 预留 |
| `m.touch.tap` | 支持，root 优先 / 无障碍 fallback | 预留 | 受限 |
| `m.touch.swipe` | 支持，root 优先 / 无障碍 fallback | 预留 | 受限 |
| `m.input.text` | 支持，root | 预留 | 受限 |
| `m.key.isAccessibilityEnabled` | 支持 | 预留 | 预留 |
| `m.device.isRootAvailable` | 支持 | 预留 | 受限 |
| `m.key.press` | 支持，root 优先 / Back、Home 无障碍 fallback | 预留 | 受限 |
| `m.key.back` | 支持，root 优先 / 无障碍 fallback | 预留 | 受限 |
| `m.key.home` | 支持，root 优先 / 无障碍 fallback | 预留 | 受限 |
| `m.screen.capture` | 支持，root 优先 / MediaProjection fallback | 预留 | 受限 |
| `m.image.release` | 支持 | 预留 | 预留 |
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
6. `docs/SCRIPT_MANUAL.md`
