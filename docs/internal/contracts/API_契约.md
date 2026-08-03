# 统一 API 契约

当前契约只记录已经按新边界整理完成的能力。

## 分层规则

固定脚本 API 的真实逻辑先放在 `libengine.so/core/api`，语言绑定层只负责参数和
返回值转换。`system_c_api` 只负责把 `core/api` 包成稳定 C ABI。

动态语言互操作不伪装成固定命令。Android `import` 由 `libengine.so` 注册 Lua
userdata 和元方法，再通过 JNI 调用统一 `JavaInteropBridge`。JS / Go 后续复用
Java 后端，但使用各自对象包装。

语言自身的并发语义同样不伪装成通用 C ABI。Lua 的 `beginThread`、
`Thread.newThread` 和 VM Gate 位于 `libengine.so/runtime/lua`；JS 和 Go 后续分别使用
自己的事件循环或 goroutine。

C ABI 统一使用 `engine_` 前缀，不带项目缩写，不暴露当前底层路线。后缀沿用已确定
脚本 API 的命名，例如 `engine_inputText`、`engine_imeSetText`，避免跨层出现不同名称。

## Android 脚本 API 设计基线

新增 Android 脚本能力先参考懒人精灵和触动精灵的公开文档。两者一致或可直接兼容时，Lua
及其他语言绑定默认保持相同的函数名称、参数顺序、可选参数、默认值、返回值和失败语义。

若两份文档存在实质差异，结合兼容性、清晰度和长期稳定性自行选择更优的一方。只有拟采用
两者之外更优的名称、参数或返回结构时，才必须先向用户说明并获得确认；不能因内部实现
方便而自行设计不兼容的公开接口。确定后再把具体接口写入本契约、公开函数页、API 总览与
`catalog.json`。

选择 Android 系统接口时遵守[0004：Android 系统接口历史参考顺序](../decisions/0004-Android系统接口历史参考顺序.md)：
只先参考 `T:\老项目` 中较新的项目实现，只有它不足以判断时才查看旧项目；历史代码不改变
本项目的当前分层或权限边界。

Android 的 Root 执行边界不属于 C ABI：固定 API 仍是 `libengine.so -> system_c_api -> AndroidBridge`。
Root 模式的 `libengine.so` 位于 RootDaemon 创建的 uid=0 一次性 Worker；截图、输入和系统控制等
稳定 Root 能力仍由 Worker 通过认证 socket 请求常驻 RootDaemon。Lua、后续 JS/Go 和插件不会
各自执行 `su`，也不需要感知 RootDaemon 的端口、令牌或 Worker 启动方式。非 Root Worker 保留
相同 ABI，并按底层实际结果返回失败，不由控制层统一改写错误或选择备用路线。

当前运行时 C ABI：

```c
engine_print
engine_logPrint
engine_sleep
engine_sleepInterruptible
engine_systemTime
engine_tickCount
engine_runtimeLastError
```

当前 ALPKG 资源 C ABI：

```c
int engine_readAlpkgFile(
        const char* relativePath,
        const unsigned char** data,
        size_t* size
);
```

`engine_readAlpkgFile` 只允许当前 ALPKG 脚本任务及其已绑定上下文的 native 子线程读取 manifest 中 `resource` 类型的
项目相对路径。成功返回 `1`，数据由 SO 当前线程持有且只读；失败返回 `0`，原因通过
`engine_runtimeLastError()` 获取。该数据地址在同线程下一次读取前有效，语言绑定必须复制
需要长期保存的数据。Lua、未来 JS/Go 和插件都通过这个 ABI 复用同一条读取路径。

当前截图 C ABI：

```c
engine_getScreenPixels
engine_setScreenPixels
engine_restoreScreenPixels
engine_keepCapture
engine_releaseCapture
engine_setCaptureCacheMs
engine_screenLastError
```

当前找色 C ABI：

```c
engine_findColors
engine_findColorsLastError
```

当前图像 C ABI：

```c
engine_capture
engine_findPic
engine_clearImageCache
engine_setImageCacheMaxBytes
engine_imageLastError
```

当前 OCR C ABI：

```c
engine_ocrLoadBuiltinModel
engine_ocrLoadModel
engine_ocrReleaseModel
engine_ocrIsModelLoaded
engine_ocrRead
engine_ocrFindText
engine_ocrLastError
```

当前点阵字库 C ABI：

```c
engine_fontSetDict
engine_fontAddDict
engine_fontUseDict
engine_fontGetPixel
engine_fontOcr
engine_fontFindStr
engine_fontFindStrEx
engine_fontFindStrFast
engine_fontFindStrFastEx
engine_fontLastError
```

当前输入 C ABI：

```c
engine_touchDown
engine_touchMove
engine_touchUp
engine_keyDown
engine_keyUp
engine_keyPress
engine_inputText
engine_getRunEnvType
engine_inputLastError
```

当前输入法 C ABI：

```c
engine_imeLock
engine_imeSetText
engine_imeUnlock
engine_imeLastError
```

当前设备 C ABI：

```c
engine_getDeviceApi
engine_appIsFront
engine_appIsRunning
engine_frontAppName
engine_getDisplayInfoJson
engine_getInstalledAppsJson
engine_exec
engine_exitScript
engine_deviceLastError
```

完整接口及 Lua 返回结构见 [脚本文档 · 设备](../../public/脚本文档.md)；实现与 C ABI 见
[Android 设备 API](../platform/android/ANDROID_设备_API.md)。

当前脚本 UI C ABI：

```c
engine_uiOpen
engine_uiUpdate
engine_uiPostMessage
engine_uiClose
engine_uiWaitEvent
engine_uiWaitEventInterruptible
engine_uiCloseAll
engine_uiLastError
```

当前 ImGui C ABI：

```c
engine_getImGuiApi
engine_imguiIsSupport
engine_imguiShow
engine_imguiClose
engine_imguiReset
engine_imguiCreateWindow
engine_imguiCreateButton
engine_imguiWaitEvent
engine_imguiWaitClosed
engine_imguiLastError
```

以上仅列出代表性入口。窗口、布局、控件、表格、图片、样式和图形的完整直接导出声明位于
`core/imgui_c_api.h`，并全部收录在 `EngineImGuiApi` 子函数表中。

当前插件函数表入口：

```c
engine_getApi
engine_getImGuiApi
```

Lua 当前通过 HostApi 暴露脚本函数，但 HostApi 只做 Lua 类型转换，并调用同一组
C ABI。JS / Go 后续也按同样方式绑定，不各写一套命令逻辑。

## 动态 Java 互操作契约

```lua
import("java.lang.*")
import("完整.Java.类名")
```

规则：

- 完整类名导入后按简单类名写入 `_G`。
- `package.*` 使用延迟类解析，首次成功解析后缓存到 `_G`。
- Java 对象在 Lua 中是 userdata，不是数字句柄。
- 支持字段、方法、构造函数、公开内部类、重载、数组、集合和接口代理。
- Java `void` 对应 Lua 无返回值；Java `null` 对应一个 `nil`。
- Java 异常转换为 Lua 错误。
- 对象由 JNI GlobalRef 保活，Lua userdata 回收或运行时结束时释放。
- `import("org.opencv.*")` 在解析类前会按需加载已导入的 `opencv/libc++_shared.so` 与
  `opencv/libopencv_java4.so`；文件缺失或 linker 失败时，`import` 直接返回 Lua 错误。

该能力没有“每个 Java 方法一条 C ABI”的函数表。实现和线程规则见
`docs/internal/platform/android/ANDROID_Java互操作.md`。

## 运行时 C ABI

```c
int engine_print(const char* text);
int engine_logPrint(const char* text);
int engine_sleep(int durationMs);
int engine_sleepInterruptible(
        int durationMs,
        runtime_interrupt_callback shouldInterrupt,
        void* userData
);
long long engine_systemTime();
long long engine_tickCount();
const char* engine_runtimeLastError();
```

说明：

- `engine_print`：普通脚本输出。
- `engine_logPrint`：日志模块输出。
- `engine_sleep`：无中断上下文的睡眠。
- `engine_sleepInterruptible`：带脚本停止回调的睡眠，Lua 的 `m.sleep` 当前使用它。
- `engine_systemTime`：系统 Unix 毫秒时间戳。
- `engine_tickCount`：当前脚本运行时间，单位毫秒。

## 截图 C ABI

```c
int engine_getScreenPixels(int* width, int* height, unsigned char** pixels);
int engine_setScreenPixels(const char* imagePath);
int engine_restoreScreenPixels();
```

参数：

- `width`：输出宽度。
- `height`：输出高度。
- `pixels`：输出点阵地址。

返回：

- `1`：成功。
- `0`：失败，通过 `engine_screenLastError()` 取错误。

点阵：

- 固定 RGBA。
- 紧凑排列。
- 长度为 `width * height * 4`。
- 内存由 `libengine.so` 持有，调用方只读、不释放。当前脚本内物理帧刷新、图片屏幕替换
  或还原会覆盖点阵内容但不更换地址；脚本任务结束后裸地址失效。

## 图片屏幕

- `engine_setScreenPixels` 支持脚本相对路径、绝对路径和当前 ALPKG 资源，成功返回 `1`。
- 图片解码后复制到当前脚本任务的固定屏幕缓冲区，宽高不得超过当前物理屏幕，不缩放、
  不裁剪。
- 激活期间 `engine_getScreenPixels`、找色、找图、点阵识字和 `engine_capture` 都读取固定图片，
  完全绕过截图缓存时间和 Root 截图。
- `engine_restoreScreenPixels` 关闭图片屏幕并使物理帧失效；下一次读取强制把实时 Root 截图
  写入同一地址。没有图片屏幕时重复调用也返回 `1`。
- 替换、还原和物理帧刷新可以覆盖裸地址中的内容，但不会释放或更换地址。
- 脚本正常结束、停止、`exitScript` 和错误退出共用任务清理路径，都会释放固定缓冲区并
  清除图片屏幕状态。

## 截图缓存

```c
void engine_keepCapture();
void engine_releaseCapture();
int engine_setCaptureCacheMs(int durationMs);
const char* engine_screenLastError();
```

规则：

- 默认缓存时间为 `20ms`。
- 缓存命中直接返回当前点阵。
- 缓存过期重新截图并覆盖缓存。
- `engine_keepCapture()` 锁帧。
- `engine_releaseCapture()` 取消锁帧。
- 图片屏幕激活期间，上述设置仍被保留但不参与读帧；还原后继续生效，但第一次读取一定
  获取实时物理帧。
- 脚本结束时释放固定缓冲区，并清除物理帧和图片屏幕的有效状态。

## 找色 C ABI

```c
typedef struct EnginePoint {
    int x;
    int y;
} EnginePoint;

int engine_findColors(
        int x1,
        int y1,
        int x2,
        int y2,
        int dir,
        int sim,
        const char* colors,
        EnginePoint* point
);
const char* engine_findColorsLastError();
```

规则：

- `engine_findColors` 直接使用当前截图缓存，不带“是否截屏”参数。
- 截图是否刷新由 `engine_getScreenPixels` 的缓存时间、`engine_keepCapture` 和 `engine_releaseCapture` 控制。
- `dir` 取值为 `1` 到 `8`，沿用旧找色算法扫描方向。
- `sim` 为默认容差，格式为 `0xRRGGBB`。
- `colors` 格式示例：`0|0|FFFFFF,10|5|FF0000-101010`。
- 找到返回 `1`，`point.x/point.y` 为命中坐标。
- 未找到或失败返回 `0`，`point.x/point.y` 为 `-1/-1`，原因通过 `engine_findColorsLastError()` 获取。

## 图像 C ABI

```c
typedef struct EngineRect {
    int left;
    int top;
    int right;
    int bottom;
} EngineRect;

int engine_capture(const char* path, const EngineRect* region);
int engine_findPic(
        int x1, int y1, int x2, int y2,
        const char* picName,
        const char* deltaColor,
        int dir,
        double sim,
        EnginePoint* point
);
const char* engine_findPicAll(
        int x1, int y1, int x2, int y2,
        const char* picName,
        const char* deltaColor,
        int dir,
        double sim
);
void engine_clearImageCache(const char* picName);
int engine_setImageCacheMaxBytes(size_t maxBytes);
const char* engine_imageLastError();
```

规则：

- `region == nullptr` 时保存全屏；非空时按左闭右开坐标保存指定区域，不交换坐标也不自动裁剪。
- `engine_capture` 是主动将当前 RGBA 截图编码并写入文件的统一接口；Lua 的 `snapShot` 直接
  指向同一个 `capture` 绑定，不重复增加 C ABI。
- `engine_findPic` 直接复用截图缓存；模板仅在首次使用、普通文件修改、LRU 淘汰或显式清理后解码。
- `engine_findPicAll` 使用相同模板、透明像素、容差和扫描方向规则，成功返回非重叠命中的
  JSON 数组（没有命中时为 `[]`），失败返回 `nullptr` 并写入 `engine_imageLastError()`。
- 模板缓存默认上限为 `5 MiB`，按预处理容器实际分配容量计算，超限时按 LRU 淘汰；
  `engine_setImageCacheMaxBytes(0)` 可关闭缓存。脚本结束后全部释放并恢复默认上限。
- 模板路径支持脚本目录相对路径、普通绝对路径和当前 `.alpkg` 的资源路径。
- 找到时返回 `1` 并写入模板左上角；未找到时返回 `0`、坐标写为 `-1/-1` 且
  `engine_imageLastError()` 为空；失败时同样返回 `0`，但错误接口有具体原因。

## OCR C ABI

```c
int engine_ocrLoadBuiltinModel(const char* name, int threads);
int engine_ocrLoadModel(
        const char* name,
        const char* detPath,
        const char* recPath,
        const char* clsPath,
        const char* keysPath,
        int threads
);
int engine_ocrReleaseModel(const char* name);
int engine_ocrIsModelLoaded(const char* name);
const char* engine_ocrRead(const char* name, const char* imagePath, const char* optionsJson);
const char* engine_ocrFindText(
        const char* name,
        const char* imagePath,
        const char* text,
        const char* optionsJson
);
const char* engine_ocrLastError();
```

规则：

- `engine_ocrLoadBuiltinModel` 从已导入的 `rapidocr/` 目录按固定相对路径加载 PP-OCRv4 mobile 中文/英文
  检测、识别、方向分类模型和字典，并在首次实际创建模型时按需加载
  `rapidocr/libonnxruntime.so`。基础 APK 不携带这些大文件；导入只复制目录，不校验内容、ABI 或依赖。
- `engine_ocrLoadModel` 加载脚本指定的 RapidOCR 兼容 PP-OCR ONNX 模型；两种入口最终共享
  相同的 session 缓存和释放规则，且同样要求已导入 `rapidocr/libonnxruntime.so`。
- 相同名称、相同配置的重复加载直接复用，不增加引用次数；不同名称的相同配置共享底层 ONNX session。
- `engine_ocrRead` 返回 `{ "items": [...] }` JSON；`engine_ocrFindText` 返回
  `{ "found": boolean, ... }` JSON。失败返回 `nullptr`，错误通过 `engine_ocrLastError()` 获取。
- 图片必须是 Android 能直接读取的普通文件。当前截图可先用 `engine_capture` 保存后再识别。

## YOLO C ABI

```c
const EngineYoloApi* engine_getYoloApi();
int engine_yoloIsAvailable();
const char* engine_yoloRuntimeInfoJson();
int engine_yoloLoadModel(
        const char* name,
        const char* labelsPath,
        const char* paramPath,
        const char* binPath,
        const char* optionsJson
);
int engine_yoloReleaseModel(const char* name);
int engine_yoloIsModelLoaded(const char* name);
const char* engine_yoloDetectScreen(
        const char* name,
        int left,
        int top,
        int right,
        int bottom,
        const char* optionsJson
);
const char* engine_yoloDetectFile(
        const char* name,
        const char* imagePath,
        const char* optionsJson
);
const char* engine_yoloLastError();
```

规则：

- `EngineYoloApi` 是始终存在的语言中立子函数表；它由 `engine_getYoloApi()` 和
  `engine_getApi()->getYoloApi()` 返回同一张只读表。顶层 `EngineApi` 需不低于 `21`，
  `EngineYoloApi::abiVersion` 当前为 `1`；两张表以后各自只在尾部追加字段。
- `libxiaoyv_yolo.so` 是可选的独立 NCNN CPU 运行时，基础 APK 永不打包它。用户把它放到
  `/sdcard/xiaoyv/extensions/yolo/` 后在 App 扩展页导入 `yolo` 目录；导入只复制为私有只读副本，
  不校验签名、哈希、版本、ABI、文件名或依赖，也不加载代码。`engine_yoloRuntimeInfoJson()` 只查询状态：
  `available` 表示 `yolo/libxiaoyv_yolo.so` 已导入、可尝试，`loaded` 表示当前引擎进程已经实际加载；模型加载或检测
  才触发加载，失败通过 `engine_yoloLastError()` 说明原因。
- `labelsPath`、`paramPath`、`binPath` 和 `imagePath` 必须是 Android 可读的普通文件路径，不能来自
  ALPKG。相同名称及相同模型配置的重复加载复用已加载模型；同名不同配置会明确失败，调用方先
  `engine_yoloReleaseModel()` 后才能按新配置加载。
- `optionsJson` 必须是 JSON 对象。内部当前支持 `input`、三个 `outputs` blob 名、`targetSize`、
  `threads`、`probThreshold`、`nmsThreshold` 与 `useGpu`；第一阶段仅 CPU，`useGpu:true` 会明确失败。
- `engine_yoloDetectScreen()` 的区域采用左闭右开坐标，四个坐标均为 `0` 时检测完整截图；其结果和
  `engine_yoloDetectFile()` 一样是 `{ "items": [{ "x": number, "y": number, "w": number,
  "h": number, "label": string, "prob": number }] }` JSON。
  截图检测先取得一份原子 RGBA 副本，结果坐标相对完整截图；文件检测坐标相对该图片。
- 所有 YOLO JSON 和错误字符串由当前线程持有，下一次同类调用可能覆盖内容。当前没有公开 Lua/JS/Go
  映射；它们确定后才进入公开函数目录和 `catalog.json`。

## 点阵字库 C ABI

```c
int engine_fontSetDict(int index, const char* dictionary);
int engine_fontAddDict(int index, const char* dictionary);
int engine_fontUseDict(int index);
const char* engine_fontGetPixel(int x1, int y1, int x2, int y2, const char* color);
const char* engine_fontOcr(int x1, int y1, int x2, int y2, const char* color, double sim);
int engine_fontFindStr(
        int x1, int y1, int x2, int y2,
        const char* text, const char* color, double sim,
        EnginePoint* point
);
const char* engine_fontFindStrEx(
        int x1, int y1, int x2, int y2,
        const char* text, const char* color, double sim
);
int engine_fontFindStrFast(
        int x1, int y1, int x2, int y2,
        const char* text, const char* color, double sim,
        EnginePoint* point
);
const char* engine_fontFindStrFastEx(
        int x1, int y1, int x2, int y2,
        const char* text, const char* color, double sim
);
const char* engine_fontLastError();
```

规则：

- 新字库格式是 `文字$宽$高$十六进制点阵`，宽高允许 `1` 到 `256`，不受旧 11 行字库限制；
  简化旧 `文字$十六进制点阵` 格式仍按 11 行兼容读取，也支持懒人
  `文字$点阵$元数据...$真实高度` 和大漠 `点阵$文字$偏移元数据$真实高度` 格式。
- 字库内容可直接传文本，也可传普通文件或当前 `.alpkg` 资源路径。
- `engine_fontUseDict` 选择当前调用线程要使用的字库。多线程中需要使用非默认字库时，各线程各自调用一次。
- 点阵字库属于当前脚本任务资源；脚本正常结束、停止、`exitScript` 或错误退出时统一清空。
- 字形保留任意宽高完整点阵；纵向 11 位特征只用于候选分桶，最终结果始终用 64 位行块比较
  全部点阵，并同时限制缺失点和多余点。
- `engine_fontFindStr` / `engine_fontFindStrEx` 先执行完整识字再查找连续文本；
  `engine_fontFindStrFast` / `engine_fontFindStrFastEx` 只搜索目标标签涉及的字形，适合大字库
  固定文本查找。
- 识字和找字均直接读取截图缓存，不保存图片、不依赖 OCR 模型；结构化结果通过 JSON 返回。

## 输入 C ABI

```c
int engine_touchDown(int id, int x, int y);
int engine_touchMove(int id, int x, int y);
int engine_touchUp(int id);
int engine_keyDown(const char* keyCode);
int engine_keyUp(const char* keyCode);
int engine_keyPress(const char* keyCode);
int engine_inputText(const char* text);
const char* engine_getRunEnvType();
const char* engine_inputLastError();
```

规则：

- 输入注入只走 RootDaemon 常驻特权进程，不走无障碍。
- `touchDown`、`touchMove`、`touchUp` 在公开 Lua 层均不返回值；C ABI 与内部 HostApi 的
  布尔结果只用于组合手势判断和其他语言绑定。
- `keyDown`、`keyUp`、`keyPress`、`inputText` 返回布尔语义。
- `keyCode` 支持数字字符串和 `Home`、`Back`、`VolUp` 等常用标识符。
- `inputText` 当前通过按键事件输入文本，适合英文、数字和常见符号。

## 输入法 C ABI

```c
int engine_imeLock();
int engine_imeSetText(const char* text);
int engine_imeUnlock();
const char* engine_imeLastError();
```

规则：

- `engine_imeLock` 保存当前默认输入法后，启用并切换到 小鱼精灵 输入法。
- `engine_imeSetText` 只通过已经活动的 小鱼精灵 输入法提交 Unicode 文本；不会重复
  执行 Root 命令，也不回退到按键注入或无障碍。
- `engine_imeUnlock` 恢复 lock 前保存的原默认输入法，并禁用 小鱼精灵 输入法。
- `engine_imeLock` / `engine_imeUnlock` 只走 RootDaemon；调用失败通过
  `engine_imeLastError` 获取原因。

## 设备 C ABI

设备函数表由 `engine_getDeviceApi()` 返回。应用状态、硬件信息、安装应用列表、系统控制和
Root shell 都进入同一个 `EngineDeviceApi`，不由 Lua、JS、Go 或插件各自直连 Android。

```c
const EngineDeviceApi* engine_getDeviceApi();
int engine_appIsFront(const char* packageName);
int engine_appIsRunning(const char* packageName);
const char* engine_frontAppName();
const char* engine_getDisplayInfoJson();
const char* engine_getInstalledAppsJson();
const char* engine_exec(const char* command, int isRet);
int engine_exitScript();
const char* engine_readPasteboard();
int engine_writePasteboard(const char* text);
const char* engine_deviceCallJson(const char* operation, const char* argumentsJson);
const char* engine_deviceLastError();
```

规则：

- `EngineApi` 和 `EngineDeviceApi` 当前 `abiVersion` 都为 `21`。版本 19 在
  `EngineDeviceApi` 的 `lastError` 之后追加 `readPasteboard`、`writePasteboard`；版本 20
  在设备子表尾部追加 `callJson`，并在顶层 `EngineApi` 尾部追加 `findPicAll`；版本 21 再在
  顶层尾部追加 `getYoloApi`。既有字段位置没有变化。函数表只能尾部追加字段；插件先检查所需
  版本再访问新增字段，旧插件继续使用已有字段时保持可用。
- 结构化结果一律以 JSON 文本从 C ABI 返回；Lua HostApi 才转换为 table。
- 设备字符串、JSON 和错误文本由调用线程持有，下一次设备调用可能覆盖内容。
- `engine_exec` 只返回 shell 合并输出，不根据命令退出码改变成功状态；调用方自行判断。
- Root 控制命令只请求常驻 RootDaemon，不重复申请 `su`，也没有无障碍回退。
- 文本剪贴板只使用 Application Context 的 Android `ClipboardManager`：读取第一条
  `ClipData` 文本，空剪贴板或非文本内容返回空字符串；写入用
  `ClipData.newPlainText(...)` 覆盖当前内容。它不走 Root 或无障碍后备路线。
- `engine_readPasteboard()` 的平台调用失败时返回 `nullptr`，错误由
  `engine_deviceLastError()` 提供；Lua 的 `readPasteboard()` 将平台失败返回为
  `nil, errorMessage`，不会与“剪贴板当前没有文本”的空字符串混淆。Lua
  `writePasteboard(text[, kind])` 成功时不返回值，平台失败抛出 Lua 错误；`kind` 省略时
  为 `0`，Android 仅接受 `0`。
- Android 12 及以上会限制后台访问系统剪贴板；系统不允许读取时，Lua 层可能得到空字符串。
- `engine_deviceCallJson(operation, argumentsJson)` 只接受 JSON 对象参数。成功返回 JSON 值
  文本，失败返回 `nullptr` 并写入 `engine_deviceLastError()`；它仍通过统一设备核心与 Android
  平台路由，不能成为绕过权限、生命周期或函数表版本的任意 JNI 通道。

## 脚本 UI C ABI

```c
long long engine_uiOpen(const char* surface, const char* specJson);
int engine_uiUpdate(long long sessionId, const char* specJson);
int engine_uiPostMessage(long long sessionId, const char* messageJson);
int engine_uiClose(long long sessionId);
const char* engine_uiWaitEvent(long long sessionId, int timeoutMs);
const char* engine_uiWaitEventInterruptible(
        long long sessionId,
        int timeoutMs,
        runtime_interrupt_callback shouldInterrupt,
        void* userData
);
void engine_uiCloseAll();
const char* engine_uiLastError();
```

规则：

- `surface` 当前支持 `dialog`、`hud`、`web`，配置和消息均使用完整 JSON 文本。
- 成功创建时 `engine_uiOpen` 返回大于 `0` 的会话 ID；失败返回 `0`，原因通过
  `engine_uiLastError()` 获取。
- `engine_uiWaitEvent*` 成功时返回 `{"type":...,"data":...}` JSON；超时也是正常事件
  `{"type":"timeout","data":null}`。失败返回空字符串。
- Lua 等脚本语言必须使用 `engine_uiWaitEventInterruptible`，这样停止脚本可以中断等待。
- Android UI 线程只把事件投递进 native 会话队列，不能直接执行语言运行时。
- `engine_uiCloseAll` 在脚本结束、停止和引擎销毁时调用，确保 App 主进程没有遗留界面。

## ImGui C ABI

```c
const EngineImGuiApi* engine_getImGuiApi();
int engine_imguiShow(const EngineImGuiSurfaceConfig* config);
void engine_imguiClose();
void engine_imguiReset();
int engine_imguiWaitEvent(
        EngineImGuiEvent* event,
        int timeoutMs,
        engine_imgui_interrupt_callback shouldInterrupt,
        void* userData
);
int engine_imguiWaitClosed(
        engine_imgui_interrupt_callback shouldInterrupt,
        void* userData
);
const char* engine_imguiLastError();
```

规则：

- `EngineApi::abiVersion` 当前为 `21`。版本 18 在顶层函数表尾部追加 `getImGuiApi`，
  版本 20 再在它后面追加 `findPicAll`，版本 21 再追加 `getYoloApi`；ImGui 子表本身仍要求调用方检查顶层版本不低于
  `18`，版本 18 及以前字段位置不变。
- `EngineImGuiApi::abiVersion` 当前为 `1`。ImGui 子函数表同样只能在尾部追加字段；调用方
  必须先检查版本，再访问自己需要的字段。
- `engine_getImGuiApi()` 与 `engine_getApi()->getImGuiApi()` 返回同一张进程级只读函数表，
  调用方不得释放或修改。
- 控件和图形句柄使用 `EngineImGuiHandle`，只在当前脚本任务内有效；脚本结束、停止、强停
  或 `engine_imguiReset()` 后全部失效。
- C ABI 不接收 Lua、JS 或 Go 的函数对象。点击、选择、滑块、窗口关闭和 `post` 事件统一
  写入 `EngineImGuiEvent` 队列，各语言绑定在自己的运行时线程消费事件并执行回调。
- RGBA 点阵参数只在调用期间读取；路径图片和 RGBA 图片最终进入同一纹理管理流程。
- 完整结构、枚举和直接导出以 `core/imgui_c_api.h` 为准；Android 生命周期和线程边界见
  `docs/internal/platform/android/ANDROID_ImGui.md`。

## 插件函数表

```c
const EngineApi* engine_getApi();
```

外部插件 so 可以通过 `engine_getApi()` 取得函数表，再使用 `getDeviceApi()` 访问设备能力，
使用 `getImGuiApi()` 访问 ImGui 能力，使用 `getYoloApi()` 查询或调用可选 YOLO 能力；运行时、
截图、找色、图像、OCR、点阵字库、输入、输入法和脚本 UI 仍位于顶层 `EngineApi`。函数表只放稳定
C 类型，不暴露 C++ 对象。

## Lua 映射

```lua
print(...)
sleep(ms)
systemTime()
tickCount()
getRunEnvType()
touchDown([id,] x, y)
touchMove([id,] x, y)
touchUp([id,] x, y)
keyDown(keycode)
keyUp(keycode)
keyPress(keycode)
inputText(text)
readPasteboard()
writePasteboard(text[, kind])
getScriptVersion()
setStopCallBack(callback)
ime.lock()
ime.setText(text)
ime.unlock()
ime.deleteChar()
ime.finishInput()
ime.keyEvent(action, keyCode)
m.sleep(ms)
m.systemTime()
m.tickCount()
m.log.print(text)
m.getScreenPixels()
m.setScreenPixels(imagePath)
m.restoreScreenPixels()
m.keepCapture()
m.releaseCapture()
m.setCaptureCacheMs(ms)
m.findColors(x1, y1, x2, y2, dir, sim, colors)
m.capture(path[, left, top, right, bottom])
m.snapShot(path[, left, top, right, bottom])
m.findPic(x1, y1, x2, y2, picName, deltaColor, dir, sim)
m.findPicAll(x1, y1, x2, y2, picName, deltaColor, dir, sim)
m.clearImageCache([picName])
m.setImageCacheMaxBytes(maxBytes)
m.ocr.loadBuiltin([name[, threads]])
m.ocr.load(name, detPath, recPath, clsPath, keysPath[, threads])
m.ocr.release(name)
m.ocr.isLoaded(name)
m.ocr.read(name, imagePath[, options])
m.ocr.findText(name, imagePath, text[, options])
m.font.setDict(index, dictionary)
m.font.addDict(index, dictionary)
m.font.useDict(index)
m.font.getFontPixel(x1, y1, x2, y2, color)
m.font.read(x1, y1, x2, y2, color, sim)
m.font.ocr(x1, y1, x2, y2, color, sim)
m.font.ocrEx(x1, y1, x2, y2, color, sim)
m.font.findStr(x1, y1, x2, y2, text, color, sim)
m.font.findStrEx(x1, y1, x2, y2, text, color, sim)
m.font.findStrFast(x1, y1, x2, y2, text, color, sim)
m.font.findStrFastEx(x1, y1, x2, y2, text, color, sim)
-- m.ime.* 是小鱼默认输入法模块；默认全局 ime 指向同一张表。
m.ime.lock()
m.ime.setText(text)
m.ime.unlock()
m.ime.deleteChar()
m.ime.finishInput()
m.ime.keyEvent(action, keyCode)
m.dialog.alert(...)
m.dialog.confirm(...)
m.dialog.input(...)
m.dialog.select(...)
m.ui.form(spec)
m.hud.show(id, spec)
m.hud.update(id, patch)
m.hud.hide(id)
m.hud.waitEvent(id, timeoutMs)
m.web.open(spec)
m.web.waitEvent(handle, timeoutMs)
m.web.postMessage(handle, data)
m.web.close(handle)
imgui.*
```

`imgui.*` 的每个固定方法都通过 `runtime/lua/imgui_lua_api` 转换参数，再调用同名语义的
`engine_imgui*` 直接 C ABI；回调函数只保存在 Lua 绑定层，不进入跨语言函数表。

## Android Lua 兼容契约

兼容层的加载、平台调用、截图坐标、节点生命周期和明确排除项见
[`ANDROID_Lua_兼容层.md`](../platform/android/ANDROID_Lua_兼容层.md)。以下家族已经成为
可维护的当前契约，不再属于“暂未定义”：

`m` 是脚本和公开文档遵守的默认 Lua 公开层。它优先采用懒人精灵或触动精灵中更清晰、可实现
的名称、参数和返回值；`_host`、Lua 桥接、Java 与 Android 平台路由是内部实现，允许独立命名
和重构，不能反向决定 `m` 的公开形状。C ABI 是独立的扩展开发契约，其命名也不约束 `m`。

| 家族 | 公开入口 | 核心约束 |
|---|---|---|
| 加密 | `cryptLib.aes_*`、`cryptLib.rsa_*` | 二进制 Lua 字符串；平台 JSON 边界仅内部 Base64 |
| 网络 | `httpGet`、`httpPost`、文件传输、WebSocket、`require("socket")`、`require("socket.http")` | 项目 HTTP/WebSocket 阻塞调用释放 VM Gate；`downloadFile` 在目标同目录完整写入临时文件后替换，失败保留原目标；LuaSocket TCP/UDP 按上游同步超时语义执行；不导出 `m.http` |
| 标准库 / FFI | Lua 5.4 `io`、`os`；`m.ffi`、全局 `ffi`、`require("ffi")` | 不复制标准库；FFI 由静态内置的 cffi-lua + libffi 提供声明、结构体、数组、浮点、回调和可变参数 C ABI；ARM64 已完成真实运行时验证，项目不要求逐 ABI 重复验收；外部库的 ABI、依赖与声明仍由调用方负责 |
| 触控 / 输入法 | `setScreenScale`、`touchDown`、`touchMove`、`touchUp`、`tap`、`longTap`、`touchMoveEx`、`swipe`、`m.ime.*` | `m` 使用布尔缩放开关和 `touchUp([id,] x, y)`；缩放和三类基础触控均无返回；`m.ime` 是正式输入法模块 |
| 图色 | 兼容取色、多点找色、找圆、字库和多模板入口 | 共用当前截图缓存；`m.findPic` 原生方向不变 |
| 设备 / 文件 | 媒体、ZIP、assets、DPI、控制栏、重启、定时器、脚本版本、结束回调、环境切换 | 无返回旧接口失败时抛错，不返回固定成功值；结束码为 0/1/2 |
| OpenCV | `cv.snapShot`、`cv.new/get/set{Point,Point2f,Int,Double,Float,Long,Byte}`、`cv.deletePtr`、`import("org.opencv.*")` | `cv.snapShot` 返回真实 Mat；Android AAR 的 Java OpenCV API 通过通用 import 访问，首次使用会按需加载已导入的 `libc++_shared.so` 与 `libopencv_java4.so`；`cv.new*` 返回首地址为实际值的 native userdata，`deletePtr` 令其立即失效 |
| 节点 | 选择器、节点对象、`nodeLib.*` | Android 无障碍短期句柄；界面变化后重新查询 |

`lr`、`cd` 是独立的旧脚本迁移命名空间。本轮不扩展或重定义它们的成员；后续兼容映射必须
单独维护，且不得改变 `m` 的正式语义。

## 暂未定义契约

其他自动化能力暂不保留旧契约，后续按实际实现重新定义。
