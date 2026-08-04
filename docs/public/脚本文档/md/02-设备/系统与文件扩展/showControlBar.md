---
params: "show: boolean"
returns: ""
---

**方法名称：** 显示或隐藏控制栏。

**语法：** `showControlBar(show)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `show` | `boolean` | 是 | `true` 显示，`false` 隐藏。 |

| 返回值 | 说明 |
|---|---|
| 无 | 修改小鱼精灵现有控制栏状态。 |

**使用示例：**

```lua
showControlBar(false)
```

**详细说明：**

该函数只操作小鱼精灵控制栏，不创建旧产品的事件或配置系统。
