---
params: "handle: integer"
returns: "integer"
---

**方法名称：** 永久删除图形对象。

**语法：** `imgui.removeShape(handle)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 要删除的图形句柄。 |

| 返回值 | 说明 |
|---|---|
| `integer` | 删除成功返回 `0`；句柄无效或已经删除返回 `-1`。 |

**详细说明：**

```lua
if imgui.removeShape(shape) ~= 0 then
    print(imgui.getLastError())
end
```

删除后句柄立即失效；位图图形关联的 GPU 纹理会由渲染线程在安全帧中释放。
