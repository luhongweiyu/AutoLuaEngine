---
params: "pointer: userdata"
returns: "无"
---

**方法名称：** 使 `cv` 值指针失效。

**语法：** `cv.deletePtr(pointer)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `pointer` | `userdata` | 是 | 由 `cv.newPoint`、`cv.newPoint2f` 或任一 `cv.newInt/Double/Float/Long/Byte` 创建的值指针。 |

| 返回值 | 说明 |
|---|---|
| 无 | 成功时无返回值。 |

**详细说明：**

调用后，值指针会立即标记为失效并清空保存的值；之后传给对应 `cv.get*` 或 `cv.set*` 会报错。
userdata 的外壳仍由 Lua 垃圾回收管理。需要在脚本中明确结束一个值指针的使用期时调用它。

```lua
local point = cv.newPoint(10, 20)
print(cv.getPoint(point).x)
cv.deletePtr(point)
```
