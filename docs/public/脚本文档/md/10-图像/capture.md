---
params: "path: string, left: integer?, top: integer?, right: integer?, bottom: integer?"
returns: "boolean 或 nil, string"
---

**方法名称：** 保存当前截图。

**语法：** `capture(path[, left, top, right, bottom])`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `path` | `string` | 是 | 输出图片路径。`.png` 为默认格式，`.jpg`、`.jpeg`、`.webp` 使用对应格式。 |
| `left` | `integer` | 否 | 截图区域左边界，必须与其余三个坐标同时提供。 |
| `top` | `integer` | 否 | 截图区域上边界，必须与其余三个坐标同时提供。 |
| `right` | `integer` | 否 | 截图区域右边界，不包含该坐标。 |
| `bottom` | `integer` | 否 | 截图区域下边界，不包含该坐标。 |

| 返回值 | 说明 |
|---|---|
| `boolean` | 保存成功时返回 `true`。 |
| `nil, string` | 截图、编码或写入失败时返回错误信息。 |

**详细说明：**

```lua
local path = "/sdcard/xiaoyv/scripts/current.png"
local ok, errorMessage = capture(path)
if not ok then
    print("保存截图失败：", errorMessage)
    return
end

print("已保存：", path)

-- 保存从 (100, 200) 到 (500, 600) 的区域，输出图片尺寸为 400x400。
assert(capture("/sdcard/xiaoyv/scripts/region.png", 100, 200, 500, 600))
```

区域坐标采用左闭右开规则，输出宽度为 `right - left`，输出高度为 `bottom - top`。省略四个
坐标时使用 `(0, 0, 屏幕宽度, 屏幕高度)`，即保存全屏。四个坐标必须同时提供，区域必须完全
位于屏幕内且宽高大于 0；无效区域返回 `nil, errorMessage:string`，不会自动交换或裁剪坐标。

此方法取得当前屏幕点阵，然后只读取目标区域并编码到文件，不会先生成全屏 Bitmap 再裁剪。
默认保存受截图缓存规则管理的物理帧；`setScreenPixels()` 激活期间则保存固定图片屏幕。只有显式
调用 `capture` 或其别名 `snapShot` 才会产生图片编码和磁盘 IO；`getScreenPixels()`、
`findPic()` 和 `findColors()` 都不会隐式保存图片。
