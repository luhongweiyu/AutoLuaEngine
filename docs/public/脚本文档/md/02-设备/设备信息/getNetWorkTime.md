---
params: "无"
returns: "string?"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 获取系统时间字符串。

**语法：** `getNetWorkTime()`

**参数说明：** 无。

| 返回值 | 说明 |
|---|---|
| `string` | NTP 成功时返回 `yyyy-MM-dd_HH-mm-ss`。 |
| `nil` | DNS、网络或 NTP 请求失败。 |

**使用示例：**

```lua
local result = getNetWorkTime()
if result then
    print(result)
else
    print("网络时间不可用")
end
```

**详细说明：**

依次向 `time.android.com`、`ntp.aliyun.com` 和 `time1.cloud.tencent.com` 的 `123`
端口发送 NTP 请求，并按设备当前地区格式化为 `yyyy-MM-dd_HH-mm-ss`。每个服务器最多等待
1 秒，全部不可用时返回 `nil`；不会用设备本地时钟伪装网络时间。
