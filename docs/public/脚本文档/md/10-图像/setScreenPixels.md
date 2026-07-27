---
params: "imagePath: string"
returns: "boolean 或 nil, string"
---

**方法名称：** 设置屏幕像素。

**语法：** `setScreenPixels(imagePath)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `imagePath` | `string` | 是 | 图片路径，支持脚本相对路径、绝对路径和当前 ALPKG 资源。 |

| 返回值 | 说明 |
|---|---|
| `boolean` | 设置成功时返回 `true`。 |
| `nil, string` | 路径、解码、屏幕尺寸或点阵校验失败时返回错误信息。 |

**详细说明：**

```lua
assert(setScreenPixels("images/test.png"))

-- 下列命令现在都读取 test.png，而不是物理屏幕。
local w, h, pixels = getScreenPixels()
local x, y = findColors(0, 0, w - 1, h - 1, 2, 0x101010, "0|0|FFFFFF")
capture("/sdcard/xiaoyv/scripts/test-copy.png")

restoreScreenPixels()
```

图片会解码为紧凑 RGBA 点阵并复制到当前脚本任务的固定屏幕缓冲区。图片宽高不得超过
当前物理屏幕，不会自动缩放或裁剪。再次调用本方法会在同一地址覆盖当前图片屏幕。

图片屏幕激活后是固定帧，不参与 `20ms` 截图缓存判断，也不会触发 Root 截图；
`getScreenPixels()`、`findColors()`、`findPic()`、点阵识字和 `capture()` 都读取该图片。
`keepCapture()`、`releaseCapture()` 和 `setCaptureCacheMs()` 仍可调用，但只影响还原后的物理截图。

调用 `restoreScreenPixels()` 可主动还原。该命令只关闭图片屏幕并使物理帧失效；下一次读取会
把实时 Root 截图写入同一地址。脚本正常结束、停止、调用 `exitScript()` 或发生错误时，引擎
也会释放固定缓冲区并清除图片屏幕状态。当前脚本内裸点阵地址不会因设置或还原而失效，
但其中内容可能被后续操作覆盖；脚本任务结束后地址失效。
