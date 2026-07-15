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
| `isOpenBySuper` | `boolean?` | 否 | 具体取值和组合规则见下方详细说明。 |

| 返回值 | 说明 |
|---|---|
| 无 | 此方法不返回值。 |

**使用示例：**

```lua
runApp("com.tencent.mm")
```

**详细说明：**

启动应用；有组件名时精确启动，否则打开启动入口。`isOpenBySuper` 为兼容参数，当前 Root
引擎始终以最高权限执行，因此传入值不改变行为。
