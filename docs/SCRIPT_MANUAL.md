# AutoLuaEngine 脚本文档

本文档面向脚本编写者，记录当前 Android + Lua 引擎已经可以直接调用的脚本 API。

参考了懒人精灵、触动精灵这类自动化工具的文档组织方式：先说明运行环境，再按日志、设备、文件、触控、按键、截图、图片句柄分类。本文档只描述 AutoLuaEngine 当前真实支持的能力，不照搬第三方函数名，也不声明尚未实现的兼容能力。

## 1. 适用范围

当前版本：

```text
平台：Android
脚本语言：Lua 5.4.8
引擎版本：0.1.0
通讯方式：ADB forward + HTTP JSON-RPC
```

当前脚本在 Android 引擎内运行。VS Code 插件或 PC 工具负责把脚本文本发送到 App，App 内部通过 native 引擎执行 Lua。

当前不支持：

- 直接兼容懒人精灵或触动精灵的完整函数名
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
log.print("log channel works")
sleep(500)

local info = device.info()
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

### 3.1 返回值规则

Lua API 统一使用下面的返回方式：

```text
成功且有结果：返回实际结果
成功但无结果：返回 true
失败：返回 nil, errorMessage
```

推荐写法：

```lua
local ok, err = file.write(file.appDataPath("demo.txt"), "hello")
if not ok then
    print("write failed:", err)
    return
end
```

### 3.2 坐标规则

触控坐标和图片坐标都使用屏幕像素坐标。

```text
左上角：0, 0
x 方向：向右增加
y 方向：向下增加
```

截图返回的图片对象中有 `width` 和 `height`，脚本应优先用它们计算坐标，避免写死某台设备的分辨率。

### 3.3 权限规则

触控和按键依赖 Android 无障碍服务：

```lua
if not key.isAccessibilityEnabled() then
    print("需要先开启无障碍服务")
    return
end
```

截图依赖 Android MediaProjection 授权。需要先在 App 中点击 `Request Screen Capture`，并在系统弹窗中确认。

### 3.4 高频截图和点阵读取规则

`screen.capture()` 返回的是 native 内存图片句柄，不是 PNG 路径，也不会写磁盘。

高频读取像素时，推荐流程：

```lua
local img, err = screen.capture()
if not img then
    print(err)
    return
end

local colors = image.getPixels(img, {
    { x = 10, y = 10 },
    { x = 20, y = 20 },
})

image.release(img)
```

不要在循环中反复把截图编码成文件再读取。后续找色、比色算法也会基于 native 内存图片句柄实现。

## 4. 全局函数

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

### 4.2 `sleep(ms)`

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
sleep(1000)
print("after")
```

注意：

- 当前会阻塞正在执行的脚本任务。
- 需要可停止的长循环时，应拆成短 `sleep`，避免脚本长时间占用执行权。

## 5. 日志 API

### 5.1 `log.print(text)`

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
local ok, err = log.print("script started")
if not ok then
    print(err)
end
```

说明：

- `log.print` 当前与 `print` 使用同一条日志通道。
- 后续如果增加日志级别，会优先扩展 `log` 模块。

## 6. 设备 API

### 6.1 `device.info()`

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
    luaVersion = "Lua 5.4"
}
```

示例：

```lua
local info = device.info()
print("platform =", info.platform)
print("engine =", info.engineVersion)
print("lua =", info.luaVersion)
```

说明：

- Lua 内部调用当前返回 `platform`、`engineVersion`、`luaVersion`。
- PC/IDE 通过 JSON-RPC 调用 `device.info` 时，还会包含 `apiLevel`、`packageName`、`httpPort` 等 Android 端信息。

## 7. 文件 API

文件 API 当前只做基础文本读写和存在性判断。第一版建议优先读写 App 私有目录，避免额外申请外部存储权限。

### 7.1 `file.appDataPath(fileName)`

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
local path = file.appDataPath("demo.txt")
print(path)
```

注意：

- 当前实现会把传入内容直接拼到 App 私有目录后面。
- 建议第一版只传普通文件名，例如 `"demo.txt"`。

### 7.2 `file.write(path, content)`

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
local path = file.appDataPath("demo.txt")
local ok, err = file.write(path, "hello")
if not ok then
    print("write failed:", err)
end
```

### 7.3 `file.read(path)`

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
local path = file.appDataPath("demo.txt")
local text, err = file.read(path)
if not text then
    print("read failed:", err)
    return
end

print(text)
```

注意：

- 当前按字节读取，不做编码转换。
- 第一版建议写入和读取 UTF-8 文本。

### 7.4 `file.exists(path)`

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
local path = file.appDataPath("demo.txt")
if file.exists(path) then
    print("file exists")
end
```

### 7.5 `file.remove(path)`

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
local path = file.appDataPath("demo.txt")
local ok, err = file.remove(path)
if not ok then
    print("remove failed:", err)
end
```

## 8. 触控 API

触控 API 当前通过 Android 无障碍服务实现。脚本调用前可以先检查无障碍状态。

```lua
if not key.isAccessibilityEnabled() then
    print("accessibility service is not enabled")
    return
end
```

### 8.1 `touch.tap(x, y)`

点击屏幕坐标。

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
local ok, err = touch.tap(300, 500)
if not ok then
    print("tap failed:", err)
end
```

常见失败：

```text
accessibility service is not enabled
touch tap failed
```

### 8.2 `touch.swipe(x1, y1, x2, y2, duration)`

从一个坐标滑动到另一个坐标。

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
local ok, err = touch.swipe(500, 1600, 500, 500, 600)
if not ok then
    print("swipe failed:", err)
end
```

## 9. 按键 API

按键 API 当前也通过 Android 无障碍服务实现。

### 9.1 `key.isAccessibilityEnabled()`

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
print("accessibility =", key.isAccessibilityEnabled())
```

### 9.2 `key.back()`

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
local ok, err = key.back()
if not ok then
    print("back failed:", err)
end
```

### 9.3 `key.home()`

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
local ok, err = key.home()
if not ok then
    print("home failed:", err)
end
```

## 10. 截图 API

### 10.1 `screen.capture()`

获取当前屏幕截图，并返回 native 内存图片句柄。

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
local img, err = screen.capture()
if not img then
    print("capture failed:", err)
    return
end

print("image id =", img.id)
print("size =", img.width, img.height)
image.release(img)
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
- 使用完必须调用 `image.release(img)`。

## 11. 图片 API

图片 API 当前只处理 `screen.capture()` 返回的图片句柄。

### 11.1 `image.release(image)`

释放图片句柄。

参数：

| 名称 | 类型 | 说明 |
|---|---|---|
| `image` | table 或 number | `screen.capture()` 返回的图片对象，或图片 ID |

返回值：

```text
成功：true
失败：nil, errorMessage
```

示例：

```lua
local img = screen.capture()
if img then
    image.release(img)
end
```

注意：

- 高频截图脚本必须主动释放旧图片。
- 已释放或不存在的图片句柄会返回错误。

### 11.2 `image.getPixel(image, x, y)`

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
local img, err = screen.capture()
if not img then
    print(err)
    return
end

local rgb, r, g, b, a = image.getPixel(img, 100, 200)
if rgb then
    print("rgb =", rgb)
    print("rgba =", r, g, b, a)
else
    print("getPixel failed:", r)
end

image.release(img)
```

说明：

- `rgb` 是整数形式的 RGB 值。
- `r`、`g`、`b`、`a` 是拆分后的通道值。
- 坐标越界会返回错误。

### 11.3 `image.getPixels(image, points)`

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
local colors, err = image.getPixels(img, {
    { x = 10, y = 10 },
    { x = 20, y = 20 },
})
```

坐标写法二，推荐用于高频场景减少表层级：

```lua
local colors, err = image.getPixels(img, {
    10, 10,
    20, 20,
    30, 30,
})
```

完整示例：

```lua
local img, err = screen.capture()
if not img then
    print(err)
    return
end

local colors, colorsErr = image.getPixels(img, {
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

image.release(img)
```

说明：

- 只读取已有图片句柄，不会自动截图。
- 批量读取比在 Lua 循环里多次调用 `image.getPixel` 更适合高频点阵。
- 当前只返回 RGB 整数，不返回每个点的 RGBA 拆分值。

## 12. 推荐脚本结构

普通自动化脚本建议写成几个小函数，便于后续迁移到 JS 或 Go 时保持 API 语义一致。

```lua
local function requireAccessibility()
    if key.isAccessibilityEnabled() then
        return true
    end

    return nil, "accessibility service is not enabled"
end

local function tapCenter()
    local ok, err = requireAccessibility()
    if not ok then
        print(err)
        return
    end

    local info = device.info()
    print("running on", info.platform)
    touch.tap(500, 1000)
end

tapCenter()
```

截图脚本建议固定使用 `pcall` 或手动释放，避免出错后忘记释放图片。

```lua
local img, err = screen.capture()
if not img then
    print(err)
    return
end

local ok, runErr = pcall(function()
    local rgb = image.getPixel(img, 0, 0)
    print("top-left =", rgb)
end)

image.release(img)

if not ok then
    error(runErr)
end
```

## 13. IDE/PC 调用说明

脚本侧只关心 Lua API。IDE/PC 侧通过统一协议控制引擎。

当前已实现的 JSON-RPC 方法：

| 方法 | 说明 |
|---|---|
| `device.info` | 获取设备和引擎信息 |
| `script.run` | 发送并执行脚本 |
| `script.stop` | 请求停止当前脚本 |
| `script.status` | 查询脚本状态 |
| `log.drain` | 轮询读取日志 |
| `screen.capture` | 从协议侧请求截图句柄 |
| `image.release` | 从协议侧释放图片句柄 |

协议细节见 [Engine Protocol](../shared/protocol/ENGINE_PROTOCOL.md)。

## 14. 与懒人精灵、触动精灵的关系

本文档参考它们的分类方式，但当前不做函数名兼容。

对应关系大致如下：

| 常见自动化能力 | AutoLuaEngine 当前 API | 状态 |
|---|---|---|
| 延时 | `sleep(ms)` | 已支持 |
| 日志 | `print(...)` / `log.print(text)` | 已支持 |
| 点击 | `touch.tap(x, y)` | 已支持，需无障碍 |
| 滑动 | `touch.swipe(...)` | 已支持，需无障碍 |
| 返回键 | `key.back()` | 已支持，需无障碍 |
| Home 键 | `key.home()` | 已支持，需无障碍 |
| 截图 | `screen.capture()` | 已支持，需截图授权 |
| 单点取色 | `image.getPixel(img, x, y)` | 已支持 |
| 批量取色 | `image.getPixels(img, points)` | 已支持 |
| 找色 / 比色 | 后续 `image.findColor` 等 | 未实现 |
| 控件查找 | 后续 `widget` 模块 | 未实现 |
| 应用启动/关闭 | 后续 `app` 模块 | 未实现 |
| FFI | 后续评估 | 未实现 |
| 启动线程 | 后续 `thread` 模块 | 未实现 |

## 15. 常见错误

### 15.1 `accessibility service is not enabled`

原因：

```text
Android 无障碍服务未开启
```

处理：

```text
到系统设置里开启 AutoLuaEngine 的无障碍服务
```

影响 API：

- `touch.tap`
- `touch.swipe`
- `key.back`
- `key.home`

### 15.2 `screen capture permission is not granted`

原因：

```text
没有完成 Android 截图授权
```

处理：

```text
在 App 中点击 Request Screen Capture，然后确认系统弹窗
```

影响 API：

- `screen.capture`

### 15.3 `image handle is not found`

原因：

```text
图片 ID 不存在，或已经被释放
```

处理：

```text
确认 image.release 不要重复调用，确认传入的是当前 screen.capture 返回的图片对象或 ID
```

影响 API：

- `image.release`
- `image.getPixel`
- `image.getPixels`

### 15.4 `open file failed`

原因：

```text
文件不存在，路径不可访问，或没有权限
```

处理：

```lua
local path = file.appDataPath("demo.txt")
file.write(path, "hello")
local text, err = file.read(path)
```

第一版建议优先使用 `file.appDataPath` 得到 App 私有路径。

## 16. 后续文档维护规则

每新增一个脚本 API，必须同步更新本文档：

1. 所属模块
2. 函数签名
3. 参数表
4. 返回值
5. 最小示例
6. 权限要求
7. 常见错误

如果 API 也暴露给 IDE/PC 协议，还需要同步更新 [Engine Protocol](../shared/protocol/ENGINE_PROTOCOL.md)。
