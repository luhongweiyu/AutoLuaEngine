---
params: "无"
returns: "table"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 获取已装应用详情。

**语法：** `getInsallAppInfos()`

**参数说明：** 无。

| 返回值 | 说明 |
|---|---|
| `table` | 返回 Lua 表；字段结构见下方详细说明。 |

**使用示例：**

```lua
local result = getInsallAppInfos()
print(result)
```

**详细说明：**

已安装应用详情数组。函数名保持设备方法文档中的拼写。

`m.getInsallAppInfos()` 中每项包含：`packageName`、`appName`、`versionName`、`versionCode`、
`apkPath`、`systemApp`。
