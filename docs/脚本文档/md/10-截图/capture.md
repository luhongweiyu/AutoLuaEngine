---
params: "无"
returns: "integer, integer, integer 或 nil, string"
---

```lua
local w, h, pixels = m.capture()
if not w then
    print("截图失败：", h)
    return
end

print("宽 =", w)
print("高 =", h)
print("点阵地址 =", string.format("0x%x", pixels))
```

成功时：

- 第 1 个返回值 `w:integer`：宽度。
- 第 2 个返回值 `h:integer`：高度。
- 第 3 个返回值 `pixels:integer`：点阵首地址。
- 点阵格式固定为紧凑 RGBA，长度为 `w * h * 4`。

失败时：

- 第 1 个返回值：`nil`。
- 第 2 个返回值 `errorMessage:string`：错误信息。


- `m.keepCapture()` 无参数，返回 `true:boolean`。
- `m.releaseCapture()` 无参数，返回 `true:boolean`。
- `m.setCaptureCacheMs(ms:integer)` 成功返回设置后的 `ms:integer`，失败返回
  `nil, errorMessage:string`。
- 默认缓存时间：`20ms`。
- 缓存时间内重复调用 `m.capture()`，复用当前点阵。
- 超过缓存时间后重新截图并覆盖内部缓存。
- `m.keepCapture()` 后一直复用当前帧。
- `m.releaseCapture()` 后恢复按时间缓存。
- 脚本运行中不会主动清空截图缓存；脚本结束后由引擎统一释放。
