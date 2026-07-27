---
params: "apkPath: string"
returns: "无"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 安装 APK。

**语法：** `installApk(apkPath)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `apkPath` | `string` | 是 | 具体取值和组合规则见下方详细说明。 |

| 返回值 | 说明 |
|---|---|
| 无 | 此方法不返回值。 |

**使用示例：**

```lua
installApk("/sdcard/Download/example.apk")
```

**详细说明：**

通过 RootDaemon 执行 APK 安装。
