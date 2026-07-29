---
params: "ctype: string 或 ctype；value/init: 可选 Lua 值或 cdata"
returns: "ctype、cdata、number、boolean、string 或 nil"
---

**方法名称：** 创建、转换和检查 C 数据与类型。

**语法：** `ffi.new(ctype[, init...])`、`ffi.cast(ctype, value)`、`ffi.typeof(ctype)`、`ffi.sizeof(ctypeOrCdata[, length])`

**参数说明：**

| 参数 | 类型 | 说明 |
|---|---|---|
| `ctype` / `ctypeOrCdata` | `string`、ctype 或 cdata | 已声明的 C 类型、声明字符串或实际 cdata。 |
| `init` | 可选 | 初始值；可用字段赋值和数组索引逐项初始化复杂数据。 |
| `value` | Lua 值或 cdata | 要转换为目标 C 类型的值。转换必须与 C ABI 相容。 |
| `length` | `integer`（可选） | 变长数组、柔性数组或按指定长度读取/计算时所需的元素数量。 |

| 返回值 | 说明 |
|---|---|
| `cdata` | `ffi.new` 分配的数据、`ffi.cast` 转换结果或 `ffi.addressof` 返回的地址。 |
| ctype | `ffi.typeof` 返回可复用的类型对象。 |
| `integer` | `ffi.sizeof`、`ffi.alignof`、`ffi.offsetof` 返回布局信息。 |
| `boolean` | `ffi.istype` 判断值是否属于指定类型。 |
| `string` / Lua number / `nil` | `ffi.string`、`ffi.tonumber` 等转换函数的结果。 |

**详细说明：**

常用数据接口包括：

| 接口 | 用途 |
|---|---|
| `ffi.new` / `ffi.cast` / `ffi.typeof` | 分配 cdata、按声明转换、缓存 ctype。 |
| `ffi.sizeof` / `ffi.alignof` / `ffi.offsetof` / `ffi.istype` | 查询类型布局或检查类型。 |
| `ffi.addressof` / `ffi.nullptr` | 取得 cdata 地址或表示空 `void*`。 |
| `ffi.string` / `ffi.copy` / `ffi.fill` | 读取 C 字符串、复制字节和填充内存。长度不明确时不要读取裸指针。 |
| `ffi.tonumber` / `ffi.type` / `ffi.eval` | 将算术 cdata 转为 Lua 数字、识别 cdata 或解析 C 数值常量。 |

结构体、数组和指针的内存布局由当前 Android ABI 决定。不要用 Lua table 代替 C 指针，也不要将任意
整数当作有效地址。若要把 `cv.new*` 等项目提供的 native userdata 交给 FFI，必须先确认其实际 C
类型和所有权；不确定时应通过正式 API 传递数据。

```lua
local ffi = require("ffi")

ffi.cdef[[
    typedef struct {
        int x;
        int y;
    } xiaoyv_point;
]]

local point = ffi.new("xiaoyv_point")
point.x, point.y = 12, 34

local buffer = ffi.new("char[64]")
ffi.fill(buffer, ffi.sizeof(buffer), 0)
ffi.copy(buffer, "xiaoyv")

assert(ffi.sizeof(point) == 8)
assert(ffi.string(buffer) == "xiaoyv")
print(point.x, point.y, ffi.tonumber(point.x))
```

上例的 `xiaoyv_point` 仅包含两个 `int`，因此当前 ABI 上大小为 8。对于含指针、`long`、浮点、
嵌套结构体或编译器填充的类型，应以 `ffi.sizeof`、`ffi.alignof` 和目标设备实测结果为准，不要把
示例中的布局推广到其他 C 类型。
