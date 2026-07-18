# 小鱼抓图取色器

`xiaoyv_tools.exe` 是小鱼精灵的独立 PC 图像工具，可单独运行，也可由 VSCode 插件启动。

本次重构后的源码按职责拆分：

- `src/model`：图片、取色点和点阵字库状态，不依赖具体窗口布局。
- `src/core`：Qt 图片与跨平台 `shared/native/image_core` 的统一适配。
- `src/workspace`：图片标签、保存关闭、待用框选和当前画布状态。
- `src/canvas`：图片标签、缩放、像素网格、放大镜、键盘定位和框选。
- `src/panels`：取色、二值化、点阵提取、字库编辑和脚本编辑界面。
- `src/device/device_transport.*`：LAN/ADB、HTTP 和 JSON-RPC 传输。
- `src/device/device_client.*`：截图、投影、脚本运行和日志业务。
- `src/capture`：浮动区域抓图和 Windows 指定窗口抓图。
- `src/generator`：有执行上限的 Lua/JavaScript 自定义生成格式。
- `src/ui`：动作目录、主题、图标和紧凑停靠窗。

构建：

```powershell
.\构建.ps1 -Configuration Release
```

生成文件：`build/xiaoyv_tools.exe`。

最终工作逻辑以 [PC 抓图取色器行为契约](../../docs/PC_抓图取色器_行为契约.md) 为准。
