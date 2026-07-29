---
params: "x: number, y: number / value: userdata"
returns: "userdata、table 或无"
---

**方法名称：** 创建、读取和修改浮点点值指针。

**语法：** `cv.newPoint2f(x, y)`、`cv.getPoint2f(value)`、`cv.setPoint2f(value, x, y)`

**参数说明：**

| 方法 | 参数 | 说明 |
|---|---|---|
| `cv.newPoint2f(x, y)` | `x, y: number` | 创建保存两个有限 `float` 坐标的值指针。 |
| `cv.getPoint2f(value)` | `value: userdata` | 读取由 `cv.newPoint2f` 创建的值。 |
| `cv.setPoint2f(value, x, y)` | `value: userdata, x, y: number` | 修改已有值。 |

| 返回值 | 说明 |
|---|---|
| `userdata` | `cv.newPoint2f` 成功时返回的值指针。 |
| `{x=number, y=number}` | `cv.getPoint2f` 返回当前坐标。 |
| 无 | `cv.setPoint2f` 成功时无返回值。 |

**详细说明：**

坐标必须是可表示为 `float` 的有限数。该值指针用于懒人兼容和 `ffi` 的指针参数，
不是 Java `org.opencv.core.Point` 对象。已删除或类型不匹配的值会报错。

```lua
local point = cv.newPoint2f(12.5, 24.75)
cv.setPoint2f(point, 30.25, 40.5)
local value = cv.getPoint2f(point)
print(value.x, value.y)
cv.deletePtr(point)
```
