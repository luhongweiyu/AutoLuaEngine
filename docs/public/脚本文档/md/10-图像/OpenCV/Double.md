---
params: "value: number / pointer: userdata"
returns: "userdata、number 或无"
---

**方法名称：** 创建、读取和修改 `Double` 值指针。

**语法：** `cv.newDouble(value)`、`cv.getDouble(pointer)`、`cv.setDouble(pointer, value)`

**参数说明：**

| 方法 | 参数 | 说明 |
|---|---|---|
| `cv.newDouble(value)` | `value: number` | 创建保存一个 Lua 数值的双精度值指针。 |
| `cv.getDouble(pointer)` | `pointer: userdata` | 读取由 `cv.newDouble` 创建的值。 |
| `cv.setDouble(pointer, value)` | `pointer: userdata, value: number` | 修改已有值。 |

| 返回值 | 说明 |
|---|---|
| `userdata` | `cv.newDouble` 成功时返回的值指针。 |
| `number` | `cv.getDouble` 返回当前值。 |
| 无 | `cv.setDouble` 成功时无返回值。 |

**详细说明：**

该接口用于需要 `double*` 形式参数的兼容或 `ffi` 调用。值指针有明确类型；不能与
`cv.Float`、`cv.Int` 等互换使用。

```lua
local value = cv.newDouble(1.25)
cv.setDouble(value, 2.5)
print(cv.getDouble(value))
cv.deletePtr(value)
```
