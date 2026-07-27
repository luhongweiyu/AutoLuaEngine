---
params: "packageName: string"
returns: "boolean"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 判断指定应用主进程是否存在。

**语法：** `appIsRunning(packageName)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `packageName` | `string` | 是 | 要查询或操作的 Android 应用包名，例如 com.tencent.mm。 |

| 返回值 | 说明 |
|---|---|
| `boolean` | true 表示指定应用主进程存在；false 表示主进程不存在。 |

**使用示例：**

```lua
local pkg = "com.tencent.mm"
print(appIsRunning(pkg))
```

**详细说明：**

指定包的主进程是否存在，需要 RootDaemon。
