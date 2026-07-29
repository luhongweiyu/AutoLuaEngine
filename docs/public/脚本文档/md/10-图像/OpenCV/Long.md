---
params: "value: integer / pointer: userdata"
returns: "userdata、integer 或无"
---

**方法名称：** 创建、读取和修改 `Long` 值指针。

**语法：** `cv.newLong(value)`、`cv.getLong(pointer)`、`cv.setLong(pointer, value)`

**参数说明：**

| 方法 | 参数 | 说明 |
|---|---|---|
| `cv.newLong(value)` | `value: integer` | 创建保存一个有符号 64 位整数的值指针。 |
| `cv.getLong(pointer)` | `pointer: userdata` | 读取由 `cv.newLong` 创建的值。 |
| `cv.setLong(pointer, value)` | `pointer: userdata, value: integer` | 修改已有值。 |

| 返回值 | 说明 |
|---|---|
| `userdata` | `cv.newLong` 成功时返回的值指针。 |
| `integer` | `cv.getLong` 返回当前值。 |
| 无 | `cv.setLong` 成功时无返回值。 |

**详细说明：**

该接口保存 Lua 5.4 整数范围内的有符号 64 位值，适合需要 `int64_t*` 形式参数的兼容或
`ffi` 调用。传入错误类型或已删除值会报错。

```lua
local value = cv.newLong(10000000000)
cv.setLong(value, 20000000000)
print(cv.getLong(value))
cv.deletePtr(value)
```
