---
params: "id: integer"
returns: "无"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 抬起触摸。

**语法：** `touchUp(id)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `id` | `integer` | 是 | 由脚本定义的 HUD 逻辑标识。 |

| 返回值 | 说明 |
|---|---|
| 无 | 此方法不返回值。 |

**详细说明：**

弹起手指，仅 Root 模式。

```lua
touchDown(0, 100, 100)
sleep(50)
local ok = touchMove(0, 200, 200)
touchUp(0)
print(ok)
```

参数：

- `id:integer`：模拟手指索引，范围 `0` 到 `4`。
- `x:integer, y:integer`：屏幕坐标。

返回：

- `touchDown`：无返回值。
- `touchMove`：返回 `boolean`，表示命令是否已执行。
- `touchUp`：无返回值。
