---
params: "见方法表"
returns: "见方法表"
---

**方法名称：** Lua 标准库时间/文件接口与受限 FFI。

**语法：** `io.open(...)`、`os.date(...)`、`ffi.cdef(...)`、`ffi.load(...)`

**参数说明：**

| 方法 | 参数 | 说明 |
|---|---|---|
| `io.*` | Lua 5.4 标准参数 | 文件、流和临时文件接口 |
| `os.date([format[, time]])` / `os.time([table])` | Lua 5.4 标准参数 | 日期格式化与秒级时间 |
| `ffi.cdef(declarations)` | C 函数原型字符串 | 注册后续允许调用的函数签名 |
| `ffi.load(nameOrPath)` | 库名、soname 或绝对路径 | 返回库对象，通过 `library.symbol(...)` 调用 |

| 返回值 | 说明 |
|---|---|
| 见 Lua 5.4 标准库 | `io` 和 `os` 保持标准 Lua 语义。 |
| 无 | `ffi.cdef` 成功后不返回值。 |
| `userdata` | `ffi.load` 返回自动关闭的动态库对象。 |

**详细说明：**

本项目运行 Lua 5.4，不额外重写已经存在的 `io`、`os.date` 和 `os.time`。毫秒时间使用
`systemTime()`，脚本运行时长使用 `tickCount()`。

`ffi` 是真实动态库调用，但不是完整 LuaJIT FFI。当前边界：

- 仅解析普通 C 函数原型；不支持结构体、数组、回调或可变参数。
- 支持 `void`、32/64 位整数、指针和 `char*`；不支持浮点参数或浮点返回值。
- `size_t`、`ssize_t`、`intptr_t`、`uintptr_t` 按当前进程位数处理；显式
  `int64_t`/`uint64_t`/`long long` 仅在 64 位进程支持，32 位进程会在 `ffi.cdef`
  阶段明确报错，避免截断。
- 单个函数最多 6 个参数。
- `char*` 参数接收 Lua 字符串；普通指针可传 `nil`、userdata、lightuserdata 或整数地址。
- `ffi.load("c")` / `ffi.load("libc")` 会加载 `libc.so`。

```lua
ffi.cdef[[
    int getpid(void);
    unsigned long strlen(const char* text);
]]

local libc = ffi.load("c")
print(libc.getpid(), libc.strlen("xiaoyv"))
```
