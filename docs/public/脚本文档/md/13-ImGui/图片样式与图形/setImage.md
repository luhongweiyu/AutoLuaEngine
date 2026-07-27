---
params: "handle: integer, path: string|nil"
returns: "无"
---

**方法名称：** 设置、替换或清空图片控件内容。

**语法：** `imgui.setImage(handle, path)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 图片控件句柄。 |
| `path` | `string` 或 `nil` | 是 | 新图片路径；`nil` 或空字符串清空图片。 |

| 返回值 | 说明 |
|---|---|
| 无 | 本方法没有返回值；解码或句柄失败时写入错误信息。 |

**详细说明：**

```lua
imgui.setImage(image, "/sdcard/xiaoyv/scripts/new.png")
imgui.setImage(image, nil)
```

替换时正在渲染的帧继续持有旧快照，不会读取已经释放的点阵；下一帧切换新纹理。
