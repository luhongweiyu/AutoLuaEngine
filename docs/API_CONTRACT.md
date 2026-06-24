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
local ok, err = file.write("/sdcard/test.txt", "hello")
if not ok then
    log.print("写入失败: " .. err)
end
```

JS 后续规则：

- 成功：返回实际结果
- 失败：抛出 Error 或返回结构化错误
- 平台不支持：抛出 `UnsupportedError`

## 3. API 命名规范

模块使用小写名：

- `log`
- `device`
- `file`
- `screen`
- `touch`
- `image`
- `thread`
- `key`

函数名使用小驼峰：

- `log.print`
- `device.info`
- `file.read`
- `file.exists`
- `file.remove`
- `screen.capture`
- `touch.tap`
- `key.isAccessibilityEnabled`
- `key.back`
- `key.home`
- `image.getPixel`
- `image.getPixels`
- `image.findColor`

Lua 可以保留一个全局 `sleep(ms)`，因为自动化脚本里使用频率极高。

## 4. HostApi v0.1

当前已注册：

- 全局 `print(...)`
- 全局 `sleep(ms)`
- `log.print(text)`
- `device.info()`
- `file.read(path)`
- `file.write(path, content)`
- `file.exists(path)`
- `file.remove(path)`
- `file.appDataPath(fileName)`
- `touch.tap(x, y)`
- `touch.swipe(x1, y1, x2, y2, duration)`
- `key.isAccessibilityEnabled()`
- `key.back()`
- `key.home()`
- `screen.capture()`
- `image.release(image)`
- `image.getPixel(image, x, y)`
- `image.getPixels(image, points)`

后续会保留全局 `sleep(ms)`，`print(...)` 作为脚本调试快捷输出保留。

### 4.1 `log.print(text)`

说明：

- 输出日志到统一日志通道
- Android 第一版转发到 Logcat
- 后续同时回传 IDE

Lua 示例：

```lua
log.print("hello")
```

返回：

```lua
true
```

失败：

```lua
nil, "message"
```

### 4.2 `sleep(ms)`

说明：

- 暂停当前脚本任务
- 单位：毫秒
- 第一版可以阻塞当前脚本线程

Lua 示例：

```lua
sleep(1000)
```

返回：

```lua
true
```

失败：

```lua
nil, "invalid duration"
```

### 4.3 `device.info()`

说明：

- 获取设备基础信息
- 第一版至少返回平台名称和引擎版本

Lua 示例：

```lua
local info = device.info()
log.print(info.platform)
```

返回示例：

```lua
{
    platform = "android",
    engineVersion = "0.1.0",
    luaVersion = "Lua 5.4"
}
```

### 4.4 `file.read(path)`

说明：

- 读取文本文件
- 第一版只支持 UTF-8 文本

Lua 示例：

```lua
local text, err = file.read("/sdcard/test.txt")
if not text then
    log.print(err)
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

### 4.5 `file.write(path, content)`

说明：

- 写入文本文件
- 第一版覆盖写入

Lua 示例：

```lua
local ok, err = file.write("/sdcard/test.txt", "hello")
```

成功：

```lua
true
```

失败：

```lua
nil, "message"
```

### 4.6 `file.exists(path)`

说明：

- 判断文件是否存在
- 第一版只做普通文件存在性判断；目录能力后续按需要再补

Lua 示例：

```lua
if file.exists(path) then
    print("exists")
end
```

返回：

```lua
true 或 false
```

### 4.7 `file.remove(path)`

说明：

- 删除文件
- 删除不存在或无权限时返回错误

Lua 示例：

```lua
local ok, err = file.remove(path)
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

### 5.1 `touch.tap(x, y)`

说明：

- 点击屏幕坐标
- Android 优先使用无障碍服务
- 如果无障碍服务未开启，返回 `nil, "accessibility service is not enabled"`

Lua 示例：

```lua
local ok, err = touch.tap(300, 500)
if not ok then
    print(err)
end
```

### 5.2 `touch.swipe(x1, y1, x2, y2, duration)`

说明：

- 滑动屏幕
- `duration` 单位毫秒
- Android 优先使用无障碍服务
- 如果无障碍服务未开启，返回 `nil, "accessibility service is not enabled"`

Lua 示例：

```lua
local ok, err = touch.swipe(300, 800, 300, 300, 500)
if not ok then
    print(err)
end
```

### 5.3 `key.isAccessibilityEnabled()`

说明：

- 返回当前无障碍服务是否已开启
- 这是只读状态检查，不会触发任何系统操作

Lua 示例：

```lua
if not key.isAccessibilityEnabled() then
    print("accessibility service is not enabled")
end
```

返回：

```lua
true 或 false
```

### 5.4 `key.back()`

说明：

- 执行系统返回键
- Android 第一版使用无障碍服务的全局动作
- 如果无障碍服务未开启，返回 `nil, "accessibility service is not enabled"`

Lua 示例：

```lua
local ok, err = key.back()
if not ok then
    print(err)
end
```

### 5.5 `key.home()`

说明：

- 执行系统主页键
- Android 第一版使用无障碍服务的全局动作
- 如果无障碍服务未开启，返回 `nil, "accessibility service is not enabled"`

Lua 示例：

```lua
local ok, err = key.home()
if not ok then
    print(err)
end
```

### 5.6 `screen.capture()`

说明：

- 获取当前屏幕截图
- Android 第一版返回内存图片句柄，不做 PNG 编码，不写磁盘
- 句柄只暴露基础元信息；后续找色、比色直接在 native 内存上处理
- Android 第一版使用 MediaProjection，需要用户确认截图授权
- 未授权时返回 `nil, "screen capture permission is not granted"`

Lua 示例：

```lua
local img, err = screen.capture()
if img then
    print(img.id, img.width, img.height, img.format)
    image.release(img)
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

### 5.6.1 `image.release(image)`

说明：

- 释放 `screen.capture()` 返回的图片句柄
- 高频截图脚本应在用完一帧后主动释放，避免 native 内存累积

Lua 示例：

```lua
local img = screen.capture()
if img then
    image.release(img)
end
```

### 5.6.2 `image.getPixel(image, x, y)`

说明：

- 从 `screen.capture()` 返回的 native 内存图片句柄读取单个像素
- 坐标从 `0` 开始
- 不重新截图、不编码 PNG、不写磁盘
- 当前返回 RGBA 拆分值，方便后续脚本或 native 算法复用

Lua 示例：

```lua
local img = screen.capture()
if img then
    local rgb, r, g, b, a = image.getPixel(img, 100, 200)
    if rgb then
        print(rgb, r, g, b, a)
    end
    image.release(img)
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

### 5.6.3 `image.getPixels(image, points)`

说明：

- 批量读取多个坐标的 RGB 值
- 用于高频点阵读取，减少 Lua 与 native 之间的调用次数
- `points` 支持 `{x1, y1, x2, y2}` 扁平数组，也支持 `{ {x=1,y=2}, {3,4} }`
- 只读取已有图片句柄，不负责找色、比色等算法

Lua 示例：

```lua
local colors = image.getPixels(img, {
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

### 5.7 `image.findColor(image, color, x1, y1, x2, y2, tolerance)`

说明：

- 在图片指定区域查找颜色
- `color` 建议使用 `#RRGGBB`
- 当前未实现。后续会基于 `screen.capture()` 的 native 内存句柄和 `image.getPixels` 的点阵读取能力继续做。

Lua 示例：

```lua
local pos = image.findColor(img, "#ff0000", 0, 0, 1080, 2400, 10)
if pos then
    touch.tap(pos.x, pos.y)
end
```

## 6. 能力表

| API | Android 第一版 | Windows 后续 | iOS 后续 |
|---|---:|---:|---:|
| `log.print` | 支持 | 预留 | 预留 |
| `sleep` | 支持 | 预留 | 预留 |
| `device.info` | 支持 | 预留 | 预留 |
| `file.read` | 支持 | 预留 | 预留 |
| `file.write` | 支持 | 预留 | 预留 |
| `file.exists` | 支持 | 预留 | 预留 |
| `file.remove` | 支持 | 预留 | 预留 |
| `touch.tap` | v0.2 | 预留 | 受限 |
| `touch.swipe` | v0.2 | 预留 | 受限 |
| `key.isAccessibilityEnabled` | 支持 | 预留 | 预留 |
| `key.back` | 支持，需无障碍 | 预留 | 受限 |
| `key.home` | 支持，需无障碍 | 预留 | 受限 |
| `screen.capture` | 支持，需用户授权 | 预留 | 受限 |
| `image.release` | 支持 | 预留 | 预留 |
| `image.getPixel` | 支持 | 预留 | 预留 |
| `image.getPixels` | 支持 | 预留 | 预留 |
| `image.findColor` | 未实现 | 预留 | 预留 |

## 7. 文档维护规则

每新增一个脚本 API，必须同步更新：

1. API 说明
2. 参数表
3. 返回值
4. Lua 示例
5. 平台能力表
6. `docs/SCRIPT_MANUAL.md`
