---
params: "value: integer / pointer: userdata"
returns: "userdata、integer 或无"
---

**方法名称：** 创建、读取和修改 `Int` 值指针。

**语法：** `cv.newInt(value)`、`cv.getInt(pointer)`、`cv.setInt(pointer, value)`

**参数说明：**

| 方法 | 参数 | 说明 |
|---|---|---|
| `cv.newInt(value)` | `value: integer` | 创建保存一个有符号 32 位整数的值指针。 |
| `cv.getInt(pointer)` | `pointer: userdata` | 读取由 `cv.newInt` 创建的值。 |
| `cv.setInt(pointer, value)` | `pointer: userdata, value: integer` | 修改已有值。 |

| 返回值 | 说明 |
|---|---|
| `userdata` | `cv.newInt` 成功时返回的值指针。 |
| `integer` | `cv.getInt` 返回当前值。 |
| 无 | `cv.setInt` 成功时无返回值。 |

**详细说明：**

`value` 必须在有符号 32 位整数范围内。该接口只接受自身创建的 `Int` 值指针；传入其他
`cv.new*` 类型或已删除的值会报错。

```lua
local value = cv.newInt(10)
cv.setInt(value, 42)
print(cv.getInt(value))
cv.deletePtr(value)
```
