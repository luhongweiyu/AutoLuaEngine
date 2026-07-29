---
params: "value: integer / pointer: userdata"
returns: "userdata、integer 或无"
---

**方法名称：** 创建、读取和修改 `Byte` 值指针。

**语法：** `cv.newByte(value)`、`cv.getByte(pointer)`、`cv.setByte(pointer, value)`

**参数说明：**

| 方法 | 参数 | 说明 |
|---|---|---|
| `cv.newByte(value)` | `value: integer` | 创建保存一个无符号字节的值指针。 |
| `cv.getByte(pointer)` | `pointer: userdata` | 读取由 `cv.newByte` 创建的值。 |
| `cv.setByte(pointer, value)` | `pointer: userdata, value: integer` | 修改已有值。 |

| 返回值 | 说明 |
|---|---|
| `userdata` | `cv.newByte` 成功时返回的值指针。 |
| `integer` | `cv.getByte` 返回当前值。 |
| 无 | `cv.setByte` 成功时无返回值。 |

**详细说明：**

字节值只能是 `0..255` 的整数。该接口用于需要单字节指针参数的兼容或 `ffi` 调用，
不能与其他 `cv.new*` 类型混用。

```lua
local value = cv.newByte(128)
cv.setByte(value, 255)
print(cv.getByte(value))
cv.deletePtr(value)
```
