---
params: "[id: integer,] x: integer, y: integer"
returns: "无"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 移动触摸。

**语法：** `touchMove([id,] x, y)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `id` | `integer` | 否 | 模拟手指索引；省略时使用 `1`。 |
| `x` | `integer` | 是 | 屏幕横坐标。 |
| `y` | `integer` | 是 | 屏幕纵坐标。 |

| 返回值 | 说明 |
|---|---|
| 无 | 此方法不返回值。 |

**详细说明：**

移动手指，仅 Root 模式。

```lua
touchDown(100, 100)
sleep(50)
touchMove(200, 200)
touchUp(200, 200)
```

参数：

- `id:integer`：模拟手指索引；省略时为 `1`。多指手势中应保持同一索引。
- `x:integer, y:integer`：屏幕坐标。

返回：

- `touchDown`：无返回值。
- `touchMove`：无返回值。
- `touchUp`：无返回值。
