# 插件 SO API

本文记录外部插件 SO 复用引擎能力的当前方式。插件只使用稳定 C ABI，不直接访问 C++ 对象，
也不依赖 Lua HostApi 的内部命名。各能力的参数、返回值、内存所有权和错误语义以
[统一 API 契约](API_契约.md)为准；本文只集中说明插件入口、版本检查和生命周期边界。

## 入口与所有权

```c
const EngineApi* engine_getApi();
const EngineDeviceApi* engine_getDeviceApi();
const EngineImGuiApi* engine_getImGuiApi();
const EngineYoloApi* engine_getYoloApi();
```

插件通常通过 `dlsym` 查找 `engine_getApi`，再从顶层函数表取得设备、ImGui 和 YOLO 子表。
三个直接子表入口返回与顶层字段相同的进程级只读函数表。所有函数表均由 `libengine.so`
持有，调用方不得释放或修改。

完整结构体、枚举和直接导出以
[system_c_api.h](../../../engines/android/app/src/main/cpp/core/system_c_api.h) 与
[imgui_c_api.h](../../../engines/android/app/src/main/cpp/core/imgui_c_api.h) 为唯一声明来源；本文
不再复制完整 `EngineApi`，避免表尾新增字段后文档布局漂移。

## 当前版本

| 函数表 | 当前版本 | 最近变化 |
|---|---:|---|
| `EngineApi` | 21 | ABI 20 尾加 `findPicAll`；ABI 21 再尾加 `getYoloApi` |
| `EngineDeviceApi` | 21 | ABI 19 尾加剪贴板；ABI 20 尾加 `callJson`；ABI 21 没有新增设备字段 |
| `EngineImGuiApi` | 1 | 独立演进的 ImGui 子函数表 |
| `EngineYoloApi` | 1 | ABI 21 引入的可选 YOLO 子函数表 |

既有字段位置保持不变，所有函数表以后都只能在尾部追加。插件必须先检查顶层版本，再检查
自己要访问的子表版本：

```c
const EngineApi* api = engine_getApi();
if (api == NULL || api->abiVersion < 12) {
    return;
}

const EngineDeviceApi* device = api->getDeviceApi();
if (device == NULL || device->abiVersion < 12) {
    return;
}
```

历史字段门槛：

- 图片屏幕 `setScreenPixels` / `restoreScreenPixels`：顶层 ABI 15。
- 预设 OCR 模型 `ocrLoadBuiltinModel`：顶层 ABI 16。
- 快速找字：顶层 ABI 17。
- `getImGuiApi`：顶层 ABI 18，再检查 `EngineImGuiApi::abiVersion`。
- 文本剪贴板：顶层和设备表均不低于 ABI 19。
- `callJson`：顶层和设备表均不低于 ABI 20。
- `findPicAll`：顶层 ABI 20。
- `getYoloApi`：顶层 ABI 21，再检查 `EngineYoloApi::abiVersion`。

第一版 APK、插件 SO、FFI 外部库和可选 native 扩展只支持 `arm64-v8a`、`x86_64`。插件
必须与设备所安装 APK 的 ABI 一致；导入成功不代表 linker 能加载 ABI 不匹配或缺依赖的 SO。

## 常用能力

设备子表包含应用状态、应用启动/停止、安装 APK、Root `exec`、硬件和屏幕信息、传感器、
网络/电话信息及系统控制。脚本签名和返回结构见[公开脚本文档](../../public/脚本文档.md)，
Android 路由见[设备 API 实现说明](../platform/android/ANDROID_设备_API.md)：

```c
const EngineDeviceApi* device = api->getDeviceApi();
int foreground = device->appIsFront("com.example.app");
const char* output = device->exec("id", 1);
const char* displayJson = device->getDisplayInfoJson();
```

文本剪贴板和通用平台 JSON 必须分别检查对应版本：

```c
if (api->abiVersion >= 19 && device->abiVersion >= 19) {
    const char* text = device->readPasteboard();
    device->writePasteboard(text == NULL ? "" : text);
}

if (api->abiVersion >= 20 && device->abiVersion >= 20) {
    const char* resultJson = device->callJson("device.isDebug", "{}");
}
```

`findPicAll` 使用与 `findPic` 相同的模板、透明像素、容差和扫描方向规则。没有命中返回
`[]`；失败返回 `nullptr`，原因通过 `imageLastError()` 获取：

```c
if (api->abiVersion >= 20) {
    const char* itemsJson = api->findPicAll(
            0, 0, 1080, 1920,
            "button.png", "000000", 1, 0.9
    );
}
```

## ImGui 子表

```c
if (api->abiVersion >= 18) {
    const EngineImGuiApi* imgui = api->getImGuiApi();
    if (imgui != NULL && imgui->abiVersion >= 1 && imgui->isSupport()) {
        EngineImGuiHandle window = imgui->createWindow(
                "插件窗口", 20, 80, 600, 420, 1
        );
    }
}
```

C ABI 不传递 Lua、JS 或 Go 的函数对象。插件通过 `waitEvent` 消费统一事件，再在自己的线程
模型中分发回调。ImGui 句柄只属于当前脚本任务；`reset`、脚本停止或 Worker 退出后失效。

## YOLO 子表状态

`EngineYoloApi` 和直接 `engine_yolo*` C ABI 已经存在；Lua 已在其上提供 `m.yolo`，JS、Go
仍未绑定，也没有新增 Java `YoloV5` 包装类。插件可以在版本检查后直接使用 C ABI：

```c
if (api->abiVersion >= 21) {
    const EngineYoloApi* yolo = api->getYoloApi();
    if (yolo != NULL && yolo->abiVersion >= 1) {
        const char* runtimeJson = yolo->runtimeInfoJson();
    }
}
```

函数表本身始终存在。`isAvailable()` 只表示用户已经导入
`yolo/libxiaoyv_yolo.so`、可以尝试加载，不代表文件名、CPU ABI、依赖或内容正确；模型加载
或检测才会按需加载 SO。模型的 labels、param、bin 和图片都是普通文件路径，不属于 ALPKG。

## 生命周期与错误规则

- `getScreenPixels` 返回当前脚本任务的固定 RGBA8888 缓冲区；插件只读、不释放。物理帧刷新、
  图片屏幕替换或还原会覆盖内容，任务结束后地址失效。
- `setScreenPixels` 支持普通文件、脚本相对文件和 ALPKG 资源；图片不得超过物理屏幕。
  `restoreScreenPixels` 切回物理屏幕。
- `capture` 的区域采用左闭右开坐标；`findPic` / `findPicAll` 复用当前截图点阵和模板缓存。
- OCR、YOLO、设备、图像和错误字符串通常由当前调用线程持有；下一次同类调用可能覆盖，
  需要长期保存时立即复制。
- `readAlpkgFile` 只在当前线程运行 `.alpkg` 且读取 manifest 中 `resource` 条目时有效；返回
  字节只读，调用方应在下次读取前复制。
- `readPasteboard` / `writePasteboard` 只使用 Android 系统文本剪贴板，不走 Root 或无障碍
  后备路线。
- `callJson` 仍由引擎核心和 Android 分发层逐项白名单实现，不能作为任意 Java/JNI 调用入口。
- `getOaid` 调用链保留，但 Android 尚未接入 MSA 或 OEM provider；当前返回 `nullptr`，不是
  `ANDROID_ID`。
- 新能力先进入 `core/api`，再挂到稳定 C ABI；插件不得绕过 Worker 生命周期直接持有内部
  C++、Java 或渲染对象。
