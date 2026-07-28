---
params: ""
returns: "string | nil, string"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 读取剪贴板。

**语法：** `readPasteboard()`

**参数说明：**

无。

| 返回值 | 说明 |
|---|---|
| `string` | 系统剪贴板第一项的文本；没有可读取的文本时返回空字符串。 |
| `nil, string` | Android 平台调用本身失败，并附带失败原因。 |

**使用示例：**

```lua
local text, err = readPasteboard()
assert(text, err)
print(text)
```

**详细说明：**

- 默认 `m` API 下，`readPasteboard()` 与 `m.readPasteboard()` 等价。
- 只读取 Android 系统文本剪贴板的第一项；图片、URI 等非文本内容按空字符串处理。
- 不需要 Root；不会改用 Root 或无障碍作为读取后备路线。
- Android 12 及以上会限制后台读取系统剪贴板。当前应用不处于系统允许读取的状态时，结果
  可能为空字符串。
