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

-- 下一次读取强制获取实时物理屏幕，之后恢复缓存时间与锁帧规则。
local w, h, pixels = getScreenPixels()
```

本方法关闭图片屏幕并使物理截图缓存失效。还原后的第一次屏幕读取必定获取实时 Root 截图，
写入与图片屏幕相同的固定地址；之后继续按当前 `keepCapture()` 状态和缓存时间工作。
脚本结束、停止或报错时，引擎会释放固定缓冲区并清除图片屏幕状态。
