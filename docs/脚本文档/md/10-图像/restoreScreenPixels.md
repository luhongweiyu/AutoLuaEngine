---
params: ""
returns: "boolean"
---

**方法名称：** 还原屏幕像素。

**语法：** `restoreScreenPixels()`

**参数说明：** 无。

| 返回值 | 说明 |
|---|---|
| `boolean` | 固定返回 `true`；没有活动图片屏幕时重复调用也成功。 |

**详细说明：**

```lua
assert(setScreenPixels("images/test.png"))
-- 在图片中完成找色、找图或点阵识字。
assert(restoreScreenPixels())

-- 后续命令重新读取物理屏幕，并恢复缓存时间与锁帧规则。
local w, h, pixels = getScreenPixels()
```

本方法只移除图片屏幕，不修改原物理截图缓存。还原后，下一次屏幕读取会按当前
`keepCapture()` 状态和缓存时间决定复用原物理帧还是获取新帧。图片点阵会在最后一个正在执行的
native 图像算法读取完成后释放。脚本结束、停止或报错时，引擎会自动执行同等清理。
