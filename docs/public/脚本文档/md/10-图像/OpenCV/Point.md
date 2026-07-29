---
params: "x: integer, y: integer / value: userdata"
returns: "userdata、table 或无"
---

**方法名称：** 创建、读取和修改整型点值指针。

**语法：** `cv.newPoint(x, y)`、`cv.getPoint(value)`、`cv.setPoint(value, x, y)`

**参数说明：**

| 方法 | 参数 | 说明 |
|---|---|---|
| `cv.newPoint(x, y)` | `x, y: integer` | 创建保存两个有符号 32 位整数坐标的值指针。 |
| `cv.getPoint(value)` | `value: userdata` | 读取由 `cv.newPoint` 创建的值。 |
| `cv.setPoint(value, x, y)` | `value: userdata, x, y: integer` | 修改已有值。 |

| 返回值 | 说明 |
|---|---|
| `userdata` | `cv.newPoint` 成功时返回的值指针。 |
| `{x=integer, y=integer}` | `cv.getPoint` 返回当前坐标。 |
| 无 | `cv.setPoint` 成功时无返回值。 |

**详细说明：**

这是与懒人 `luaopenov` 对齐的 Point 值指针接口，可作为 `ffi` 接受的指针参数。它不是
Java `org.opencv.core.Point` 对象；如需调用 Java OpenCV 方法，请通过 `import` 创建对应 Java 对象。
已删除、类型不匹配或并非本接口创建的值会报错。

```lua
local point = cv.newPoint(100, 200)
cv.setPoint(point, 300, 400)
local value = cv.getPoint(point)
print(value.x, value.y)
cv.deletePtr(point)
```
