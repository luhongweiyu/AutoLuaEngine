---
params: "packageName: string"
returns: "boolean"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 判断指定应用是否处于前台运行。

**语法：** `appIsFront(packageName)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `packageName` | `string` | 是 | 要查询或操作的 Android 应用包名，例如 com.tencent.mm。 |

| 返回值 | 说明 |
|---|---|
| `boolean` | true 表示指定应用当前处于前台；false 表示不处于前台。 |

**使用示例：**

```lua
local pkg = "com.tencent.mm"

if not appIsFront(pkg) then
    runApp(pkg)
end
```

**详细说明：**

指定包是否为当前前台应用，需要 RootDaemon 查询系统窗口状态。
