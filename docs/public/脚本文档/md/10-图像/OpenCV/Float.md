---
params: "value: number / pointer: userdata"
returns: "userdata、number 或无"
---

**方法名称：** 创建、读取和修改 `Float` 值指针。

**语法：** `cv.newFloat(value)`、`cv.getFloat(pointer)`、`cv.setFloat(pointer, value)`

**参数说明：**

| 方法 | 参数 | 说明 |
|---|---|---|
| `cv.newFloat(value)` | `value: number` | 创建保存一个有限 `float` 的值指针。 |
| `cv.getFloat(pointer)` | `pointer: userdata` | 读取由 `cv.newFloat` 创建的值。 |
| `cv.setFloat(pointer, value)` | `pointer: userdata, value: number` | 修改已有值。 |

| 返回值 | 说明 |
|---|---|
| `userdata` | `cv.newFloat` 成功时返回的值指针。 |
| `number` | `cv.getFloat` 返回当前值。 |
| 无 | `cv.setFloat` 成功时无返回值。 |

**详细说明：**

传入值必须为可表示为 `float` 的有限数。该接口用于需要 `float*` 形式参数的兼容或
`ffi` 调用，不能与其他 `cv.new*` 类型混用。

```lua
local value = cv.newFloat(0.5)
cv.setFloat(value, 0.75)
print(cv.getFloat(value))
cv.deletePtr(value)
```
