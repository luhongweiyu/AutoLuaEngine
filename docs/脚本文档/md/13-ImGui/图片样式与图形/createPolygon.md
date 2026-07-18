---
params: "points: table, color: integer, closed/filled: boolean, thickness: number"
returns: "integer 或 nil"
---

**方法名称：** 创建多边形或折线图形。

**语法：** `imgui.createPolygon(points, color, closed, filled, thickness)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `points` | `table` | 是 | 顶点列表，支持 `{{x,y}}`、`{{x=,y=}}` 或平面数组。 |
| `color` | `integer` | 是 | `0xAARRGGBB` 颜色。 |
| `closed` | `boolean` | 是 | 非填充时是否连接最后一点和第一点。 |
| `filled` | `boolean` | 是 | 是否按凸多边形填充。 |
| `thickness` | `number` | 是 | 非填充线宽，必须大于 `0`。 |

| 返回值 | 说明 |
|---|---|
| `integer` | 成功返回图形句柄。 |
| `nil` | 顶点不足、坐标或线宽无效。 |

**详细说明：**

```lua
local polygon = imgui.createPolygon(
    {{20, 20}, {140, 40}, {80, 130}},
    0xFF4CC9F0, true, false, 3
)
```

填充模式使用 Dear ImGui 的凸多边形填充；凹多边形应拆分为多个凸多边形。
