---
params: "无"
returns: "integer, integer, integer 或 nil, string"
---

**方法名称：** 获取屏幕像素。

**语法：** `getScreenPixels()`

**参数说明：** 无。

| 返回值 | 说明 |
|---|---|
| `integer, integer, integer` | 成功时依次返回屏幕宽度、屏幕高度和点阵首地址。 |
| `nil, string` | 获取失败时返回 `nil` 和错误信息。 |

**详细说明：**

```lua
local w, h, pixels = getScreenPixels()
if not w then
    print("获取屏幕像素失败：", h)
    return
end

print("宽 =", w)
print("高 =", h)
print("点阵地址 =", string.format("0x%x", pixels))
```

成功时：

- 第 1 个返回值 `w:integer`：屏幕宽度。
- 第 2 个返回值 `h:integer`：屏幕高度。
- 第 3 个返回值 `pixels:integer`：点阵首地址。
- 点阵格式固定为紧凑 RGBA，长度为 `w * h * 4`。
- 点阵由 `libengine.so` 持有，只能读取，脚本不能释放或修改。
- 缓存过期、分辨率变化、调用 `setScreenPixels()` / `restoreScreenPixels()` 或脚本结束后，
  原地址可能失效，不能跨帧长期保存。

默认缓存时间为 `20ms`。缓存时间内重复调用 `getScreenPixels()` 会复用当前点阵；超过缓存
时间后重新截图并覆盖内部缓存。`keepCapture()` 可锁住当前帧，`releaseCapture()` 恢复按时间
缓存。`setScreenPixels(imagePath)` 激活后返回图片的宽、高和固定点阵地址，不再按时间刷新；
`restoreScreenPixels()` 后重新使用物理截图规则。
