---
params: "packageName: string, componentName: string?, isOpenBySuper: boolean?"
returns: "无"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 启动指定 Android 应用。

**语法：** `runApp(packageName[, componentName[, isOpenBySuper]])`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `packageName` | `string` | 是 | 要查询或操作的 Android 应用包名，例如 com.tencent.mm。 |
| `componentName` | `string?` | 否 | 可选的完整组件名；不传时使用应用默认启动入口。 |
| `isOpenBySuper` | `boolean?` | 否 | `true` 通过常驻 RootDaemon 启动；默认 `false` 使用 Android 普通启动 Intent。 |

| 返回值 | 说明 |
|---|---|
| 无 | 此方法不返回值。 |

**使用示例：**

```lua
runApp("com.tencent.mm")
```

**详细说明：**

启动应用；有组件名时精确启动，否则打开启动入口。普通模式可用于无障碍环境，但受 Android
后台启动限制；确实需要越过该限制时传 `isOpenBySuper=true`，此时设备必须已具备可用的
RootDaemon。
