---
params: "text: string"
returns: "boolean"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 提交文本。

**语法：** `imeLib.setText(text)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `text` | `string` | 是 | 要输出、显示或输入的文本。 |

| 返回值 | 说明 |
|---|---|
| `boolean` | 返回 true 或 false；各状态的具体含义见下方详细说明。 |

**详细说明：**

`imeLib` 是 小鱼精灵 自己的无界面输入法。它在锁定时保存系统原默认输入法，
在解锁时恢复原输入法；高频 `setText` 直接调用活动输入法的 `InputConnection`，不
重复执行 Root 命令，也不回退到按键注入或无障碍。

```lua
local locked = imeLib.lock()
if not locked then
    print("输入法锁定失败")
    return
end

local ok = imeLib.setText("你好，小鱼精灵")
print(ok)

imeLib.unlock()
```

函数说明：

- `imeLib.lock()`：无参数；保存当前默认输入法，启用并切换到 小鱼精灵 输入法；
  返回 `boolean`。
- `imeLib.setText(text:string)`：向当前获得焦点的输入框提交完整 Unicode 文本；返回
  `boolean`。
- `imeLib.unlock()`：无参数；恢复 `lock()` 前保存的默认输入法，并禁用 小鱼精灵
  输入法；返回 `boolean`。

要求：

- `lock()` / `unlock()` 需要 Root helper 可用。
- 调用 `setText()` 前，目标输入框必须已经获得焦点。
- `setText()` 前必须成功调用过 `lock()`；调用结束后应调用 `unlock()` 恢复用户原输入法。
- 自有 API 也可用 `m.ime.lock()`、`m.ime.setText(text)`、`m.ime.unlock()`。
