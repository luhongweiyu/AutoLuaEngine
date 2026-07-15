---
params: "keycode: string | integer"
returns: "boolean"
---

按下按键不弹起，仅 Root 模式。

```lua
keyDown("home")
sleep(100)
keyUp("home")

local ok = keyPress("Back")
print(ok)
```

支持的按键标识符：

| 名称 | 标识符 | 按键码 |
|---|---|---|
| 主页键 | `Home` | `3` |
| 返回键 | `Back` | `4` |
| 打电话 | `Call` | `5` |
| 挂电话 | `EndCall` | `6` |
| 音量增加 | `VolUp` | `24` |
| 音量减少 | `VolDown` | `25` |
| 电源键 | `Power` | `26` |
| 相机键 | `Camera` | `27` |
| 菜单键 | `Menu` | `82` |
| 向上翻页 | `PageUp` | `92` |
| 向下翻页 | `PageDown` | `93` |

`keyDown`、`keyUp`、`keyPress` 的 `keycode` 类型均为 `string | integer`，可以传标识符，
也可以直接传数字按键码；三个函数都返回 `boolean`，表示命令是否已执行。
