---
params: "ms: integer"
returns: "integer | nil, string?"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 设置截图缓存时间。

**语法：** `setCaptureCacheMs(ms)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `ms` | `integer` | 是 | 时长，单位为毫秒。 |

| 返回值 | 说明 |
|---|---|
| `integer | nil, string?` | 具体字段、特殊值和失败情况见下方详细说明。 |

**使用示例：**

```lua
local result = setCaptureCacheMs(0)
print(result)
```

**详细说明：**

- `m.keepCapture()` 无参数，返回 `true:boolean`。
- `m.releaseCapture()` 无参数，返回 `true:boolean`。
- `m.setCaptureCacheMs(ms:integer)` 成功返回设置后的 `ms:integer`，失败返回
  `nil, errorMessage:string`。
- 默认缓存时间：`20ms`。
- 缓存时间内重复调用 `m.capture()`，复用当前点阵。
- 超过缓存时间后重新截图并覆盖内部缓存。
- `m.keepCapture()` 后一直复用当前帧。
- `m.releaseCapture()` 后恢复按时间缓存。
- 脚本运行中不会主动清空截图缓存；脚本结束后由引擎统一释放。
