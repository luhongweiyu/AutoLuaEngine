---
params: ""
returns: ""
---

HTML 中可直接使用 `xiaoyv`：

```javascript
xiaoyv.emit("submit", { account: "demo" });
xiaoyv.close();
xiaoyv.call("device.info", {}, "device-info");

xiaoyv.onMessage = function (data) {
  // Lua m.web.postMessage(page, data)
};

xiaoyv.onCallResult = function (callbackId, result, error) {
  // xiaoyv.call 异步结果
};

window.addEventListener("xiaoyv-message", function (event) {
  console.log(event.detail);
});
```

- `xiaoyv.emit(type, data)`：转为 Lua `event.data`
- `xiaoyv.close()`：关闭页面并投递 `closed`
- `xiaoyv.call(method, params, callbackId)`：引擎 JSON-RPC，结果走 `onCallResult`
- `m.web.postMessage`：触发 `onMessage` 与 `xiaoyv-message`

这是受信任脚本环境：WebView 允许任意 URL、本地文件、网络访问、跨域文件访问、混合内容、弹窗和 JavaScript bridge。页面和脚本由同一用户控制，接口不额外限制页面来源或 RPC 方法。
打开/等待等见左侧「HTML」。
