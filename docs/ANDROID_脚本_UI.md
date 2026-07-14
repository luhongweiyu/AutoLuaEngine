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
- HUD 定时关闭、Activity 返回或 HTML 页面调用 `xiaoyv.close()` 后，脚本会收到
  `closed` 事件；脚本可再调用关闭方法回收会话。

## 原生弹窗和表单

### `m.dialog.alert`

```lua
local ok, errorMessage = m.dialog.alert("提示", "操作完成", "知道了")
```

`m.dialog.*` 和 `m.ui.form` 使用 WindowManager 悬浮窗口，和旧项目脚本 UI 一样不会启动
Activity：屏幕其余区域不变暗，点击框外区域会穿透到下方应用且不会关闭对话框。用户只能点击
对话框自己的确认/取消按钮；脚本停止或引擎关闭时才会由系统主动关闭。

原生弹窗、表单和 HUD 需要 小鱼精灵 的悬浮窗权限；权限未开启时创建会返回
`nil, errorMessage:string`。

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
})
```

### `m.dialog.select`

```lua
local index, value = m.dialog.select("选择模式", { "快", "稳", "调试" }, 2)
```

类型和返回值：

| 方法 | 参数类型 | 成功/用户操作返回值 | 失败返回值 |
|---|---|---|---|
| `m.dialog.alert` | `title:string, message:string, buttonText:string?` | `boolean` | `nil, errorMessage:string` |
| `m.dialog.confirm` | `title:string, message:string, positiveText:string?, negativeText:string?` | `boolean` | `nil, errorMessage:string` |
| `m.dialog.input` | `title:string, hint:string, defaultText:string?, options:table?` | `text:string`；取消为 `nil` | `nil, errorMessage:string` |
| `m.dialog.select` | `title:string, items:table<string>, selectedIndex:integer?, options:table?` | `index:integer, value:string`；取消为 `nil` | `nil, errorMessage:string` |

`m.dialog.select` 的索引从 `1` 开始。`options` 中的 `message`、`inputType`、
`positiveText`、`negativeText` 是 `string`，`multiline`、`selectAll` 是 `boolean`。

### `m.ui.form`

```lua
local values, errorMessage = m.ui.form({
    title = "登录参数",
    message = "设置完成后确认",
    positiveText = "开始",
    negativeText = "取消",
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
布尔值，`values.mode` 是选中的文字。`m.ui.form(spec:table)` 确认后返回 `values:table`，
取消返回 `nil`，创建失败返回 `nil, errorMessage:string`。每个字段的 `name`、`label`、
`type`、`hint` 是 `string`，`items` 是 `table<string>`，`selectedIndex` 是 `integer`；
`default` 按字段类型使用 `string` 或 `boolean`。

## HUD

```lua
local handle, errorMessage = m.hud.show("progress", {
    text = "正在处理 0%",
    x = 20,
    y = 160,
    w = 260,
    h = 72,
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

| 方法 | 参数类型 | 返回值类型 |
|---|---|---|
| `m.hud.show(id, spec)` | `id:any, spec:table` | `handle:integer` 或 `nil, errorMessage:string` |
| `m.hud.update(id, patch)` | `id:any, patch:table` | `boolean` 或 `nil, errorMessage:string` |
| `m.hud.hide(id)` | `id:any` | `boolean` 或 `nil, errorMessage:string` |
| `m.hud.waitEvent(id[, timeoutMs])` | `id:any, timeoutMs:integer?` | `event:table` 或 `nil, errorMessage:string` |
| `m.toast(text[, durationMs])` | `text:any, durationMs:integer?` | `handle:integer` 或 `nil, errorMessage:string` |

HUD 的 `text`、`gravity`、颜色、`clickId` 是 `string`；`x`、`y`、`w`、`h`、
`padding`、`radius`、`durationMs` 是 `integer`；`textSize`、`alpha` 是 `number`；
`clickable`、`draggable` 是 `boolean`；`buttons` 是 `table<table>`。`durationMs` 大于 `0`
时 HUD 自动关闭并向脚本发送 `{ type = "closed", data = { reason = "timeout" } }`。

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
    file = "panel.html", -- 相对于 /sdcard/xiaoyv/scripts
    x = 100,
    y = 200,
    w = 800,
    h = 600,
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

| 方法 | 参数类型 | 返回值类型 |
|---|---|---|
| `m.web.open(spec)` | `spec:table` | `handle:integer` 或 `nil, errorMessage:string` |
| `m.web.waitEvent(handle[, timeoutMs])` | `handle:integer, timeoutMs:integer?` | `event:table` 或 `nil, errorMessage:string` |
| `m.web.postMessage(handle, data)` | `handle:integer, data:any` | `boolean` 或 `nil, errorMessage:string` |
| `m.web.close(handle)` | `handle:integer` | `boolean` 或 `nil, errorMessage:string` |

`m.web.open(spec)` 的页面来源优先级是 `html`、`url`、`file`。三者及 `baseUrl`、`title`
的类型均为 `string`：

`x`、`y`、`w`、`h` 的类型均为整数，单位都是屏幕物理像素。未传 `w` 和 `h` 时
保持全屏；传入任意尺寸时显示为无背景变暗的定位窗口，未传的另一个尺寸占满对应方向。
尺寸和位置超出屏幕时会限制在当前屏幕范围内。

```lua
-- HTML 字符串
m.web.open({
    html = "<button onclick=\"xiaoyv.emit('tap',{ok:true})\">确定</button>",
    baseUrl = "file:///sdcard/xiaoyv/scripts/",
})

-- 任意 URL
m.web.open({ url = "https://example.com/panel.html" })

-- 本地文件或相对脚本文件
m.web.open({ file = "panel.html" })
```

`m.html` 是 `m.web` 的别名。

### JavaScript bridge

HTML 首段脚本就可以使用 `xiaoyv`：

```javascript
xiaoyv.emit("submit", { account: "demo" });
xiaoyv.close();
xiaoyv.call("device.info", {}, "device-info");

xiaoyv.onMessage = function (data) {
  // Lua m.web.postMessage(page, data) 的数据
};

xiaoyv.onCallResult = function (callbackId, result, error) {
  // xiaoyv.call 的异步回调
};

window.addEventListener("xiaoyv-message", function (event) {
  console.log(event.detail);
});
```

- `xiaoyv.emit(type, data)`：将任意 JSON 数据转为 Lua `event.data`。
- `xiaoyv.close()`：关闭页面，并投递 `closed` 事件。
- `xiaoyv.call(method, params, callbackId)`：调用现有引擎 JSON-RPC，结果回调给
  `xiaoyv.onCallResult(callbackId, result, error)`。
- `m.web.postMessage(page, data)`：调用 `xiaoyv.onMessage(data)`，同时派发
  `xiaoyv-message` 浏览器事件。

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

| 方法 | 参数类型 | 返回值类型 |
|---|---|---|
| `m.ui.open(surface, spec)` | `surface:string, spec:table` | `sessionId:integer` 或 `nil, errorMessage:string` |
| `m.ui.update(sessionId, spec)` | `sessionId:integer, spec:table` | `boolean` 或 `nil, errorMessage:string` |
| `m.ui.postMessage(sessionId, data)` | `sessionId:integer, data:any` | `boolean` 或 `nil, errorMessage:string` |
| `m.ui.close(sessionId)` | `sessionId:integer` | `boolean` 或 `nil, errorMessage:string` |
| `m.ui.waitEvent(sessionId[, timeoutMs])` | `sessionId:integer, timeoutMs:integer?` | `event:table` 或 `nil, errorMessage:string` |
| `m.ui.closeAll()` | 无 | `boolean` |

对应跨语言 C ABI 位于：

```text
engines/android/app/src/main/cpp/core/system_c_api.h
```

`EngineApi` 函数表同样包含 UI API，供以后 JS、Go 与插件 so 复用。
