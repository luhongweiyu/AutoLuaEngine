---
params: "maxBytes: integer"
returns: "integer 或 nil, string"
---

**方法名称：** 设置找图模板缓存上限。

**语法：** `setImageCacheMaxBytes(maxBytes)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `maxBytes` | `integer` | 是 | 当前脚本允许缓存的模板内存字节数；`0` 表示关闭缓存。 |

| 返回值 | 说明 |
|---|---|
| `integer` | 设置成功时返回实际采用的 `maxBytes`。 |
| `nil, string` | 参数为负数或超出当前平台可表示范围时返回错误信息。 |

**详细说明：**

```lua
-- 默认值为 5 MiB；这里改为 8 MiB。
setImageCacheMaxBytes(8 * 1024 * 1024)

-- 关闭模板缓存。findPic 仍可使用，但每次都需要重新读取并预处理模板。
setImageCacheMaxBytes(0)
```

缓存按模板预处理后实际分配的点阵容量计算，不按 PNG/JPEG 文件大小或模板数量计算。缩小上限
时会立即按最近最少使用顺序淘汰，单个模板大于上限时仍可完成当前 `findPic()`，但不会保留在
缓存中。

设置只对当前脚本任务有效。脚本结束时模板缓存会全部释放，上限恢复为默认
`5 * 1024 * 1024` 字节。
