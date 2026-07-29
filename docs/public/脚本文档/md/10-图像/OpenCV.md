---
params: "见方法表"
returns: "见方法表"
---

**方法名称：** OpenCV 截图与基础值指针接口。

**语法：** `cv.snapShot(...)`、`cv.newPoint(...)`、`cv.getPoint(...)` 等。

**参数说明：**

| 方法 | 参数 | 返回值 |
|---|---|---|
| `cv.snapShot(left, top, right, bottom)` | 左闭右开截图区域；全 `0` 为全屏 | Java OpenCV `Mat` 或 `nil` |
| `cv.newPoint(x, y)` / `cv.newPoint2f(x, y)` | 整数或浮点坐标 | native `userdata` |
| `cv.getPoint(value)` / `cv.getPoint2f(value)` | 对应指针 | `{x=..., y=...}` |
| `cv.setPoint(value, x, y)` / `cv.setPoint2f(...)` | 指针和新坐标 | 无 |
| `cv.newInt/Double/Float/Long/Byte(value)` | 标量 | native `userdata` |
| `cv.getInt/Double/Float/Long/Byte(value)` | 对应指针 | 标量 |
| `cv.setInt/Double/Float/Long/Byte(value, newValue)` | 指针和新值 | 无 |
| `cv.deletePtr(value)` | 由 `cv.new*` 创建的指针 | 无 |

| 返回值 | 说明 |
|---|---|
| `userdata?` | `cv.snapShot` 返回真实 `org.opencv.core.Mat`；失败为 `nil`。 |
| `userdata` | `cv.new*` 返回由 Lua 自动托管、也可显式删除的 native 值指针。 |
| `number` 或 `table` | 读取标量或点时按上表返回。 |

**详细说明：**

`cv.snapShot` 从当前截图缓存生成 RGBA `Mat`，因此 `keepCapture` 和
`LuaEngine.setSnapCacheBitmap` 的固定画面同样生效。返回的 `Mat` 使用完后应调用其
`release()`。

`cv.new*` 的 userdata 首地址保存对应 Point 或标量值，可传给本项目受限 FFI 中的普通
指针参数。它不拥有其他 native 对象；`cv.deletePtr` 会令其失效，后续再读取或修改会报错。
未显式删除时由 Lua 垃圾回收。`Byte` 只接受 `0..255`。

```lua
local point = cv.newPoint(10, 20)
cv.setPoint(point, 30, 40)
local value = cv.getPoint(point)
print(value.x, value.y)
cv.deletePtr(point)

local mat = cv.snapShot(0, 0, 0, 0)
if mat then
    print(mat:cols(), mat:rows())
    mat:release()
end
```
