---
params: "parent: integer, path: string?, width/height: number?"
returns: "integer 或 nil"
---

**方法名称：** 创建图片显示控件。

**语法：** `imgui.createImage(parent[, path[, width[, height]]])`

**参数说明：**

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|---|---|---|---|---|
| `parent` | `integer` | 是 | - | 父容器句柄。 |
| `path` | `string` 或 `nil` | 否 | `nil` | 普通文件、脚本相对路径或当前 ALPKG 图片资源。 |
| `width` / `height` | `number` | 否 | `0` | `0` 使用图片原始尺寸，`-1` 使用可用空间。 |

| 返回值 | 说明 |
|---|---|
| `integer` | 成功返回图片控件句柄。 |
| `nil` | 图片解码、参数或父句柄无效。 |

**详细说明：**

```lua
local image = assert(imgui.createImage(parent, "images/status.png", 96, 96))
local empty = assert(imgui.createImage(parent, nil, 96, 96))
```

路径图片在 `libengine.so` 中保存为紧凑 RGBA8888 快照，GL 纹理由渲染线程创建和回收。
