---
params: "path: string, left: integer?, top: integer?, right: integer?, bottom: integer?"
returns: "boolean 或 nil, string"
---

**方法名称：** 保存截图别名。

**语法：** `snapShot(path[, left, top, right, bottom])`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `path` | `string` | 是 | 输出图片路径，支持 PNG、JPEG 和 WebP。 |
| `left` | `integer` | 否 | 截图区域左边界，必须与其余三个坐标同时提供。 |
| `top` | `integer` | 否 | 截图区域上边界，必须与其余三个坐标同时提供。 |
| `right` | `integer` | 否 | 截图区域右边界，不包含该坐标。 |
| `bottom` | `integer` | 否 | 截图区域下边界，不包含该坐标。 |

| 返回值 | 说明 |
|---|---|
| `boolean` | 保存成功时返回 `true`。 |
| `nil, string` | 截图、区域校验、编码或写入失败时返回错误信息。 |

**详细说明：**

```lua
assert(snapShot("/sdcard/xiaoyv/scripts/full.png"))
assert(snapShot("/sdcard/xiaoyv/scripts/region.png", 100, 200, 500, 600))
```

`snapShot` 是 `capture` 的完整别名，参数、返回值、左闭右开的区域规则、截图缓存和编码行为
完全相同。省略区域时保存 `(0, 0, 屏幕宽度, 屏幕高度)`。
