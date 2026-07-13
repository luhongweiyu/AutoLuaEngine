# Android 脚本 UI

## 目标

脚本可以按用途选择三类界面：

| 类型 | Lua 入口 | 适合场景 |
|---|---|---|
| 原生弹窗/表单 | `m.dialog.*`、`m.ui.form` | 少量参数、确认、输入和选择 |
| HUD 悬浮层 | `m.hud.*`、`m.toast` | 运行状态、提示、悬浮按钮和轻交互 |
| HTML 页面 | `m.web.*`、`m.html.*` | 复杂布局、表格、图表和网页交互 |

这三类 UI 均由 App 主进程绘制。Lua 运行在 `:engine` 进程，UI 事件通过 native 会话队列
回到原脚本线程，因此 Android UI 线程不会直接进入 Lua VM。

## 统一会话

`libengine.so` 中的 `core/api/ui_api` 为每个界面分配一个整数会话 ID。语言绑定只转换
参数，不保存 Android `View` 或 `Activity`。

```text
Lua API
  -> engine_uiOpen(surface, specJson)
  -> AndroidBridge
  -> App UI 组件

用户操作 / 页面 JS
  -> EngineHttpServer: ui.event
  -> deliverUiEvent(sessionId, type, data)
  -> engine_uiWaitEventInterruptible
  -> Lua event table
```

会话在以下情况关闭：

- 脚本主动调用 `m.ui.close`、`m.hud.hide` 或 `m.web.close`。
- 脚本执行结束、请求停止或引擎服务销毁。
- HUD 定时关闭、Activity 返回或 HTML 页面调用 `AutoLua.close()` 后，脚本会收到
  `closed` 事件；脚本可再调用关闭方法回收会话。

## 原生弹窗和表单

### `m.dialog.alert`

```lua
local ok, errorMessage = m.dialog.alert("提示", "操作完成", "知道了")
```

### `m.dialog.confirm`

```lua
local accepted, errorMessage = m.dialog.confirm("确认", "是否继续？", "继续", "取消")
```

### `m.dialog.input`

```lua
local text, errorMessage = m.dialog.input("输入", "请输入名称", "默认名称", {
    message = "该值会传给脚本",
    multiline = false,
    inputType = "text", -- text、number、password
    selectAll = true,
    positiveText = "保存",
    negativeText = "取消",
    cancelable = true,
})
```

### `m.dialog.select`

```lua
local index, value = m.dialog.select("选择模式", { "快", "稳", "调试" }, 2)
```

索引从 `1` 开始。

### `m.ui.form`

```lua
local values, errorMessage = m.ui.form({
    title = "登录参数",
    message = "设置完成后确认",
    positiveText = "开始",
    negativeText = "取消",
    cancelable = true,
    fields = {
        { name = "account", label = "账号", type = "text", hint = "账号" },
        { name = "password", label = "密码", type = "password" },
        { name = "count", label = "次数", type = "number", default = "1" },
        { name = "notice", label = "说明文字", type = "label" },
        { name = "enabled", label = "启用", type = "boolean", default = true },
        {
            name = "mode",
            label = "模式",
            type = "select",
            items = { "快", "稳" },
            selectedIndex = 1,
        },
    },
})
```

确认后 `values` 以 `name` 为键。例如 `values.account` 是字符串，`values.enabled` 是
布尔值，`values.mode` 是选中的文字。

## HUD

```lua
local handle, errorMessage = m.hud.show("progress", {
    text = "正在处理 0%",
    x = 20,
    y = 160,
    gravity = "top_left", -- top_left、center、bottom
    backgroundColor = "#CC202124",
    textColor = "#FFFFFFFF",
    textSize = 14,
    alpha = 1,
    padding = 16,
    radius = 8,
    clickable = true,
    draggable = true,
    clickId = "progress",
    buttons = {
        { id = "cancel", text = "取消" },
    },
})
assert(handle, errorMessage)

m.hud.update("progress", { text = "正在处理 50%" })

local event, waitError = m.hud.waitEvent("progress", 30000)
if event and event.type == "click" then
    print(event.data.id, event.data.text)
end

m.hud.hide("progress")
```

HUD API：

```lua
m.hud.show(id, spec)        -- handle | nil, errorMessage
m.hud.update(id, patch)     -- true | nil, errorMessage
m.hud.hide(id)              -- true / false / nil, errorMessage
m.hud.waitEvent(id, timeoutMs)
m.toast(text, durationMs)
```

`x` 和 `y` 是屏幕坐标；`width` 和 `height` 可固定 HUD 尺寸。`durationMs` 大于 `0` 时
HUD 自动关闭并向脚本发送 `{ type = "closed", data = { reason = "timeout" } }`。

事件：

```lua
{
    type = "click" | "closed" | "error" | "timeout",
    data = { ... },
}
```

- 点击 HUD 本体时，`data.id` 是 `clickId`，默认 `hud`。
- 点击按钮时，`data.id` 和 `data.text` 来自按钮配置。
- `timeoutMs` 到期时 `type` 是 `timeout`，不关闭 HUD。

## HTML 页面

### 打开和等待

```lua
local page, errorMessage = m.web.open({
    title = "控制面板",
    file = "panel.html", -- 相对于 /sdcard/AutoLuaEngine/scripts
    x = 100,
    y = 200,
    width = 800,
    height = 600,
})
assert(page, errorMessage)

while true do
    local event, waitError = m.web.waitEvent(page, -1)
    if not event then
        error(waitError)
    end
    if event.type == "closed" then
        break
    end
    if event.type == "submit" then
        m.web.postMessage(page, { accepted = true })
    end
end
m.web.close(page)
```

`m.web.open(spec)` 的页面来源优先级是 `html`、`url`、`file`：

`x`、`y`、`width`、`height` 的单位都是屏幕物理像素。未传 `width` 和 `height` 时
保持全屏；传入任意尺寸时显示为无背景变暗的定位窗口，未传的另一个尺寸占满对应方向。
尺寸和位置超出屏幕时会限制在当前屏幕范围内。

```lua
-- HTML 字符串
m.web.open({
    html = "<button onclick=\"AutoLua.emit('tap',{ok:true})\">确定</button>",
    baseUrl = "file:///sdcard/AutoLuaEngine/scripts/",
})

-- 任意 URL
m.web.open({ url = "https://example.com/panel.html" })

-- 本地文件或相对脚本文件
m.web.open({ file = "panel.html" })
```

`m.html` 是 `m.web` 的别名。

### JavaScript bridge

HTML 首段脚本就可以使用 `AutoLua`：

```javascript
AutoLua.emit("submit", { account: "demo" });
AutoLua.close();
AutoLua.call("device.info", {}, "device-info");

AutoLua.onMessage = function (data) {
  // Lua m.web.postMessage(page, data) 的数据
};

AutoLua.onCallResult = function (callbackId, result, error) {
  // AutoLua.call 的异步回调
};

window.addEventListener("autolua-message", function (event) {
  console.log(event.detail);
});
```

- `AutoLua.emit(type, data)`：将任意 JSON 数据转为 Lua `event.data`。
- `AutoLua.close()`：关闭页面，并投递 `closed` 事件。
- `AutoLua.call(method, params, callbackId)`：调用现有引擎 JSON-RPC，结果回调给
  `AutoLua.onCallResult(callbackId, result, error)`。
- `m.web.postMessage(page, data)`：调用 `AutoLua.onMessage(data)`，同时派发
  `autolua-message` 浏览器事件。

这是受信任脚本环境：WebView 允许任意 URL、本地文件、网络访问、跨域文件访问、混合内容、
弹窗和 JavaScript bridge。页面和脚本由同一用户控制，接口不额外限制页面来源或 RPC 方法。

## 通用底层 API

高级绑定可以直接使用 `m.ui`：

```lua
local sessionId, errorMessage = m.ui.open("hud", { text = "原始 HUD" })
local event = m.ui.waitEvent(sessionId, 1000)
m.ui.close(sessionId)
```

可用函数：

```lua
m.ui.open(surface, spec)
m.ui.update(sessionId, spec)
m.ui.postMessage(sessionId, data)
m.ui.close(sessionId)
m.ui.waitEvent(sessionId, timeoutMs)
m.ui.closeAll()
```

对应跨语言 C ABI 位于：

```text
engines/android/app/src/main/cpp/core/system_c_api.h
```

`EngineApi` 函数表同样包含 UI API，供以后 JS、Go 与插件 so 复用。
