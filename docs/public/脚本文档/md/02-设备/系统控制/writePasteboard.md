---
params: "text: string, kind: integer?"
returns: "无"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 写入剪贴板。

**语法：** `writePasteboard(text[, kind])`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `text` | `string` | 是 | 要写入系统剪贴板的文本；可传空字符串以覆盖并清空当前文本内容。 |
| `kind` | `integer` | 否 | 默认 `0`。Android 只支持文本剪贴板，因此只能省略或传 `0`；传其他值会报参数错误。 |

| 返回值 | 说明 |
|---|---|
| 无 | 成功时不返回值；平台写入失败会抛出 Lua 错误。 |

**使用示例：**

```lua
writePasteboard("小鱼精灵")
```

**详细说明：**

- 默认 `m` API 下，`writePasteboard(text[, kind])` 与
  `m.writePasteboard(text[, kind])` 等价。
- `kind=1` 是外部脚本约定中的 iOS 图片模式；当前 Android 引擎不支持图片剪贴板，不能传入。
- 写入使用 Android 系统文本剪贴板，不需要 Root，也不会启用 Root 或无障碍后备路线。
