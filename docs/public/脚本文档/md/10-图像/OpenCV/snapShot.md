---
params: "left: integer, top: integer, right: integer, bottom: integer"
returns: "userdata 或 nil"
---

**方法名称：** 获取 OpenCV `Mat` 截图。

**语法：** `cv.snapShot(left, top, right, bottom)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `left` | `integer` | 是 | 区域左边界。 |
| `top` | `integer` | 是 | 区域上边界。 |
| `right` | `integer` | 是 | 区域右边界，不包含该列。 |
| `bottom` | `integer` | 是 | 区域下边界，不包含该行。 |

四个参数均为 `0` 时表示完整当前画面。

| 返回值 | 说明 |
|---|---|
| `userdata` | 成功时返回真实的 `org.opencv.core.Mat`。 |
| `nil` | 截图不可用、区域越界或 OpenCV 初始化失败。 |

**详细说明：**

该函数与取色、找图共用当前截图缓存，因此 `keepCapture()`、`releaseCapture()` 和固定图片屏幕的
行为同样生效。返回的图像是 RGBA `Mat`；使用完后必须调用 `release()`，中间创建的 `Mat` 也应释放。

```lua
local image = assert(cv.snapShot(0, 0, 0, 0))
print(image:cols(), image:rows())
image:release()
```

需要调用 OpenCV 算法时，再通过 `import("org.opencv.*")` 导入相应 Java 类；见
[OpenCV 概述](概述.md)。
