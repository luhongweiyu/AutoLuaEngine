/**
 * 文件用途：声明稳定 C ABI 门面，供 Lua HostApi、JS/Go 绑定和外部 so 复用。
 */
#pragma once

#include <stddef.h>
#include "imgui_c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 脚本中断检查回调。
 *
 * 返回非 0 表示当前脚本应停止；返回 0 表示继续等待。
 */
typedef int (*runtime_interrupt_callback)(void* userData);

/**
 * C ABI 通用坐标。
 *
 * 找到目标时 x/y 为命中坐标；未找到或失败时调用方会收到 -1/-1。
 */
typedef struct EnginePoint {
    int x;
    int y;
} EnginePoint;

/**
 * C ABI 图像区域，采用左闭右开坐标。
 *
 * width = right - left，height = bottom - top。传给截图接口的区域必须完全位于屏幕内。
 */
typedef struct EngineRect {
    int left;
    int top;
    int right;
    int bottom;
} EngineRect;

/**
 * 设备能力函数表。
 *
 * 所有成员都是 libengine.so 的稳定 C ABI。字符串和 JSON 返回指针均由当前调用线程持有，
 * 调用方只读且不得释放；下一次同类调用可能覆盖该内容。JSON 结果用于 Lua table、JS 对象
 * 和 Go struct 等跨语言结构化转换，避免把某一语言的容器类型暴露进 ABI。
 */
typedef struct EngineDeviceApi {
    int abiVersion;

    int (*appIsFront)(const char* packageName);
    int (*appIsRunning)(const char* packageName);
    const char* (*frontAppName)();
    const char* (*getCurrentActivity)();
    int (*runApp)(const char* packageName, const char* componentName, int isOpenBySuper);
    int (*stopApp)(const char* packageName);
    int (*runIntent)(const char* intentJson);
    int (*installApk)(const char* apkPath);
    const char* (*getInstalledApkJson)();
    const char* (*getInstalledAppsJson)();
    const char* (*getInsallAppInfosJson)();
    int (*getApkVerInt)();

    const char* (*exec)(const char* command, int isRet);
    int (*exitScript)();

    int (*getBatteryLevel)();
    const char* (*getBoard)();
    const char* (*getBootLoader)();
    const char* (*getBrand)();
    const char* (*getCpuAbi)();
    const char* (*getCpuAbi2)();
    int (*getCpuArch)();
    const char* (*getDevice)();
    const char* (*getDeviceId)();
    int (*getDisplayDpi)();
    const char* (*getDisplayInfoJson)();
    int (*getDisplayRotate)();
    int (*getDisplaySize)(int* width, int* height);
    const char* (*getFingerprint)();
    const char* (*getHardware)();
    const char* (*getId)();
    const char* (*getManufacturer)();
    const char* (*getModel)();
    const char* (*getNetWorkTime)();
    const char* (*getOaid)();
    const char* (*getOsVersionName)();
    const char* (*getPackageName)();
    const char* (*getProduct)();
    int (*getRunEnvType)();
    const char* (*getSdPath)();
    int (*getSdkVersion)();
    const char* (*getSensorsInfoJson)();
    const char* (*getSimSerialNumber)();
    const char* (*getSubscriberId)();
    const char* (*getWifiMac)();
    const char* (*getWorkPath)();

    int (*lockScreen)();
    int (*unLockScreen)();
    int (*setDisplayPowerOff)(int isPowerOff);
    int (*setAirplaneMode)(int enabled);
    int (*setBTEnable)(int enabled);
    int (*setWifiEnable)(int enabled);
    int (*phoneCall)(const char* number, int state);
    int (*sendSms)(const char* number, const char* content);
    int (*vibrate)(int durationMs);

    const char* (*lastError)();
} EngineDeviceApi;

/**
 * 给外部插件 so 使用的函数表。
 *
 * 插件可以通过 engine_getApi() 取得这张表，然后调用同一套引擎能力，不需要
 * 自己重复声明每个 dlsym 符号。
 */
typedef struct EngineApi {
    int abiVersion;
    const char* (*getVersion)();
    const char* (*getCapabilitiesJson)();
    int (*print)(const char* text);
    int (*logPrint)(const char* text);
    int (*sleep)(int durationMs);
    int (*sleepInterruptible)(
            int durationMs,
            runtime_interrupt_callback shouldInterrupt,
            void* userData
    );
    long long (*systemTime)();
    long long (*tickCount)();
    const char* (*runtimeLastError)();
    int (*getScreenPixels)(int* width, int* height, unsigned char** pixels);
    void (*keepCapture)();
    void (*releaseCapture)();
    int (*setCaptureCacheMs)(int durationMs);
    const char* (*screenLastError)();
    int (*findColors)(
            int x1,
            int y1,
            int x2,
            int y2,
            int dir,
            int sim,
            const char* colors,
            EnginePoint* point
    );
    const char* (*findColorsLastError)();
    int (*touchDown)(int id, int x, int y);
    int (*touchMove)(int id, int x, int y);
    int (*touchUp)(int id);
    int (*keyDown)(const char* keyCode);
    int (*keyUp)(const char* keyCode);
    int (*keyPress)(const char* keyCode);
    int (*inputText)(const char* text);
    const char* (*getRunEnvType)();
    const char* (*inputLastError)();
    int (*imeLock)();
    int (*imeSetText)(const char* text);
    int (*imeUnlock)();
    const char* (*imeLastError)();
    long long (*uiOpen)(const char* surface, const char* specJson);
    int (*uiUpdate)(long long sessionId, const char* specJson);
    int (*uiPostMessage)(long long sessionId, const char* messageJson);
    int (*uiClose)(long long sessionId);
    const char* (*uiWaitEvent)(long long sessionId, int timeoutMs);
    const char* (*uiWaitEventInterruptible)(
            long long sessionId,
            int timeoutMs,
            runtime_interrupt_callback shouldInterrupt,
            void* userData
    );
    void (*uiCloseAll)();
    const char* (*uiLastError)();
    int (*readAlpkgFile)(
            const char* relativePath,
            const unsigned char** data,
            size_t* size
    );
    const EngineDeviceApi* (*getDeviceApi)();
    int (*capture)(const char* path, const EngineRect* region);
    int (*findPic)(
            int x1,
            int y1,
            int x2,
            int y2,
            const char* picName,
            const char* deltaColor,
            int dir,
            double sim,
            EnginePoint* point
    );
    void (*clearImageCache)(const char* picName);
    const char* (*imageLastError)();
    int (*ocrLoadModel)(
            const char* name,
            const char* detPath,
            const char* recPath,
            const char* clsPath,
            const char* keysPath,
            int threads
    );
    int (*ocrReleaseModel)(const char* name);
    int (*ocrIsModelLoaded)(const char* name);
    const char* (*ocrRead)(const char* name, const char* imagePath, const char* optionsJson);
    const char* (*ocrFindText)(
            const char* name,
            const char* imagePath,
            const char* text,
            const char* optionsJson
    );
    const char* (*ocrLastError)();
    int (*fontSetDict)(int index, const char* dictionary);
    int (*fontAddDict)(int index, const char* dictionary);
    int (*fontUseDict)(int index);
    const char* (*fontGetPixel)(int x1, int y1, int x2, int y2, const char* color);
    const char* (*fontOcr)(
            int x1,
            int y1,
            int x2,
            int y2,
            const char* color,
            double sim
    );
    int (*fontFindStr)(
            int x1,
            int y1,
            int x2,
            int y2,
            const char* text,
            const char* color,
            double sim,
            EnginePoint* point
    );
    const char* (*fontFindStrEx)(
            int x1,
            int y1,
            int x2,
            int y2,
            const char* text,
            const char* color,
            double sim
    );
    const char* (*fontLastError)();
    int (*setImageCacheMaxBytes)(size_t maxBytes);
    int (*setScreenPixels)(const char* imagePath);
    int (*restoreScreenPixels)();
    int (*ocrLoadBuiltinModel)(const char* name, int threads);
    int (*fontFindStrFast)(
            int x1,
            int y1,
            int x2,
            int y2,
            const char* text,
            const char* color,
            double sim,
            EnginePoint* point
    );
    const char* (*fontFindStrFastEx)(
            int x1,
            int y1,
            int x2,
            int y2,
            const char* text,
            const char* color,
            double sim
    );
    const EngineImGuiApi* (*getImGuiApi)();
} EngineApi;

/**
 * 小鱼精灵 native 稳定 C ABI。
 *
 * 真实逻辑在 core/api；本文件只暴露跨语言稳定入口。语言绑定层只负责参数转换
 * 和返回值封装，不在 Lua/JS/Go 各自重复实现命令逻辑。
 */
const char* engine_getVersion();

/**
 * 返回当前 native 能力边界的 JSON 描述。
 *
 * 返回指针由 libengine.so 内部持有，调用方只读，不要释放。
 */
const char* engine_getCapabilitiesJson();

/**
 * 返回给外部插件 so 使用的引擎函数表。
 *
 * 返回指针由 libengine.so 内部持有，调用方只读，不要释放。
 */
const EngineApi* engine_getApi();

/** 返回设备能力函数表，供插件、JS 和 Go 取得与 Lua 相同的设备 API。 */
const EngineDeviceApi* engine_getDeviceApi();

/**
 * 输出普通脚本日志。
 *
 * 返回 1 表示成功，返回 0 表示失败。
 */
int engine_print(const char* text);

/**
 * 输出日志模块文本。
 *
 * 当前和 engine_print 同级别输出；保留独立入口方便后续区分 print 与 log。
 */
int engine_logPrint(const char* text);

/**
 * 不带中断检查的睡眠。
 *
 * 参数单位为毫秒。返回 1 表示完成，返回 0 表示失败。
 */
int engine_sleep(int durationMs);

/**
 * 可中断睡眠。
 *
 * 参数单位为毫秒。shouldInterrupt 可为空；不为空时，睡眠过程中会定期调用它。
 */
int engine_sleepInterruptible(
        int durationMs,
        runtime_interrupt_callback shouldInterrupt,
        void* userData
);

/**
 * 返回最近一次运行时 C ABI 失败原因。
 *
 * 返回指针由 libengine.so 内部持有，调用方只读，不要释放。
 */
const char* engine_runtimeLastError();

/**
 * 读取当前 ALPKG 脚本任务中的一个 resource 文件。
 *
 * relativePath 必须是 manifest.json 中声明的项目相对路径；本函数不会读取包外文件，
 * 也不会返回 Lua 加密字节码。成功返回 1，并写入二进制数据首地址和长度；空文件也是
 * 成功，长度为 0。失败返回 0，data/size 会被清零，原因通过 engine_runtimeLastError()
 * 读取。
 *
 * 返回数据由 libengine.so 当前线程持有，只读且无需释放；其有效期到同线程下一次
 * engine_readAlpkgFile 调用或当前脚本任务结束。JS、Go 和插件应在脚本任务线程内复制
 * 所需字节，不得把该地址跨线程或跨回调长期保存。
 */
int engine_readAlpkgFile(
        const char* relativePath,
        const unsigned char** data,
        size_t* size
);

/**
 * Android 设备与应用 API。
 *
 * bool 风格接口成功返回 1、失败返回 0；失败原因可通过 engine_deviceLastError() 读取。
 * Lua 层会按脚本文档把少数“无返回值”命令转换为无返回值，不把 C ABI 的状态码泄露给脚本。
 */
int engine_appIsFront(const char* packageName);
int engine_appIsRunning(const char* packageName);
const char* engine_frontAppName();
const char* engine_getCurrentActivity();
int engine_runApp(const char* packageName, const char* componentName, int isOpenBySuper);
int engine_stopApp(const char* packageName);
int engine_runIntent(const char* intentJson);
int engine_installApk(const char* apkPath);
const char* engine_getInstalledApkJson();
const char* engine_getInstalledAppsJson();
const char* engine_getInsallAppInfosJson();
int engine_getApkVerInt();

/**
 * 以当前 RootDaemon 权限执行 shell。isRet 非 0 时返回合并输出，0 时执行但返回空字符串；
 * 命令退出码不作为成功条件，脚本应按返回文本自行判断。
 */
const char* engine_exec(const char* command, int isRet);
int engine_exitScript();

int engine_getBatteryLevel();
const char* engine_getBoard();
const char* engine_getBootLoader();
const char* engine_getBrand();
const char* engine_getCpuAbi();
const char* engine_getCpuAbi2();
int engine_getCpuArch();
const char* engine_getDevice();
const char* engine_getDeviceId();
int engine_getDisplayDpi();
const char* engine_getDisplayInfoJson();
int engine_getDisplayRotate();
int engine_getDisplaySize(int* width, int* height);
const char* engine_getFingerprint();
const char* engine_getHardware();
const char* engine_getId();
const char* engine_getManufacturer();
const char* engine_getModel();
const char* engine_getNetWorkTime();
const char* engine_getOaid();
const char* engine_getOsVersionName();
const char* engine_getPackageName();
const char* engine_getProduct();
int engine_getRunEnvTypeCode();
const char* engine_getSdPath();
int engine_getSdkVersion();
const char* engine_getSensorsInfoJson();
const char* engine_getSimSerialNumber();
const char* engine_getSubscriberId();
const char* engine_getWifiMac();
const char* engine_getWorkPath();

int engine_lockScreen();
int engine_unLockScreen();
int engine_setDisplayPowerOff(int isPowerOff);
int engine_setAirplaneMode(int enabled);
int engine_setBTEnable(int enabled);
int engine_setWifiEnable(int enabled);
int engine_phoneCall(const char* number, int state);
int engine_sendSms(const char* number, const char* content);
int engine_vibrate(int durationMs);

/** 返回当前线程最近一次设备 API 失败原因。 */
const char* engine_deviceLastError();

/**
 * 返回系统时间戳，单位毫秒。
 *
 * 该值是 Unix epoch 毫秒时间戳，适合记录真实世界时间。
 */
long long engine_systemTime();

/**
 * 返回当前脚本运行时间，单位毫秒。
 *
 * 语言运行时会在顶层脚本开始执行前记录起点；主任务和全部 native 子线程共享该起点。
 */
long long engine_tickCount();

/**
 * 获取屏幕像素。
 *
 * 参数：
 * - width：输出屏幕宽度。
 * - height：输出屏幕高度。
 * - pixels：输出点阵首地址，格式固定为紧凑 RGBA，长度为 width * height * 4。
 *
 * 返回：
 * - 1：成功，width/height/pixels 都已写入。
 * - 0：失败，可通过 engine_screenLastError() 读取错误文本。
 *
 * 注意：
 * pixels 指向 libengine.so 当前脚本任务的固定屏幕缓冲区，调用方只读，不要释放。
 * 物理帧刷新和图片屏幕设置/替换/还原只会覆盖内容；脚本任务结束后地址失效。
 */
int engine_getScreenPixels(int* width, int* height, unsigned char** pixels);

/**
 * 把图片设置为当前活动屏幕点阵。
 *
 * imagePath 支持脚本相对路径、绝对路径和当前 ALPKG 资源。图片激活期间不按截图缓存时间刷新；
 * 成功返回 1，失败返回 0，错误通过 engine_screenLastError() 获取。
 */
int engine_setScreenPixels(const char* imagePath);

/** 关闭图片屏幕并使物理帧失效；下一次读取实时截图到同一地址。可重复调用，成功返回 1。 */
int engine_restoreScreenPixels();

/**
 * 锁定当前截图帧。
 *
 * 开启后 engine_getScreenPixels 会一直返回当前缓存帧；如果当前还没有缓存帧，
 * 下一次 engine_getScreenPixels 会先截图并锁住这一帧。
 */
void engine_keepCapture();

/**
 * 取消锁帧，恢复按缓存时间判断是否重新截图。
 */
void engine_releaseCapture();

/**
 * 设置截图缓存时间，单位毫秒。
 *
 * 返回 1 表示设置成功，返回 0 表示参数非法。
 */
int engine_setCaptureCacheMs(int durationMs);

/**
 * 返回最近一次屏幕取帧或缓存控制调用失败原因。
 *
 * 返回指针由 libengine.so 内部持有，调用方只读，不要释放。
 */
const char* engine_screenLastError();

/**
 * 在当前屏幕截图缓存上执行多点找色。
 *
 * 参数：
 * - x1/y1/x2/y2：查找范围。
 * - dir：扫描方向，取值 1 到 8，语义沿用旧找色算法。
 * - sim：默认颜色容差，格式为 0xRRGGBB。
 * - colors：颜色描述，例如 "0|0|FFFFFF,10|5|FF0000-101010"。
 * - point：输出命中坐标。
 *
 * 返回：
 * - 1：找到颜色，point 写入命中坐标。
 * - 0：未找到或失败，point 写入 -1/-1，可通过 engine_findColorsLastError() 读取原因。
 */
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

/**
 * 返回最近一次找色 C ABI 调用失败原因。
 *
 * 返回指针由 libengine.so 内部持有，调用方只读，不要释放。
 */
const char* engine_findColorsLastError();

/**
 * 保存当前截图缓存为图片文件。
 *
 * region 为 nullptr 时保存全屏；非空时保存指定的左闭右开区域。
 * 成功返回 1；失败返回 0，原因通过 engine_imageLastError() 获取。普通截图缓存不会因为
 * 此接口以外的调用产生 PNG/JPEG 编码或磁盘 IO。
 */
int engine_capture(const char* path, const EngineRect* region);

/**
 * 在当前截图缓存中查找模板图片。
 *
 * picName 支持当前脚本目录下相对路径、绝对路径和当前 ALPKG 包资源。deltaColor 例如
 * "101010"，sim 取值为 0 到 1 之间。找到时返回 1 并写入 point；未找到或失败返回 0，
 * point 写入 -1/-1。失败原因通过 engine_imageLastError() 获取。
 */
int engine_findPic(
        int x1,
        int y1,
        int x2,
        int y2,
        const char* picName,
        const char* deltaColor,
        int dir,
        double sim,
        EnginePoint* point
);

/** 清理全部模板缓存，picName 非空时只清理对应图片缓存。 */
void engine_clearImageCache(const char* picName);

/**
 * 设置当前脚本任务的找图模板缓存上限，单位字节。
 *
 * 设置为 0 会关闭模板缓存；缩小上限时立即按 LRU 淘汰。成功返回 1。
 */
int engine_setImageCacheMaxBytes(size_t maxBytes);

/** 返回最近一次找图或截图保存 C ABI 失败原因。 */
const char* engine_imageLastError();

/**
 * 加载 RapidOCR PP-OCR ONNX 模型。
 *
 * detPath、recPath、keysPath 必填，clsPath 可为空。相同 name 且同配置重复调用会直接复用；
 * 不同配置必须先 release，避免脚本运行中无提示替换模型。
 */
int engine_ocrLoadModel(
        const char* name,
        const char* detPath,
        const char* recPath,
        const char* clsPath,
        const char* keysPath,
        int threads
);

/** 加载 APK 内置中文 PP-OCRv4 mobile 模型；成功返回 1，失败返回 0。 */
int engine_ocrLoadBuiltinModel(const char* name, int threads);

/** 释放指定 OCR 模型名称的引用。 */
int engine_ocrReleaseModel(const char* name);

/** 查询指定 OCR 模型名称是否已加载。 */
int engine_ocrIsModelLoaded(const char* name);

/**
 * 识别普通图片文件。
 *
 * 返回由 libengine.so 当前线程持有的 JSON：{"items":[{"text", "x", "y", "w", "h", "score"}]}。
 * 失败返回 nullptr，原因通过 engine_ocrLastError() 获取。
 */
const char* engine_ocrRead(const char* name, const char* imagePath, const char* optionsJson);

/** 在图片 OCR 结果中查找文字，返回 found/x/y/w/h/text/score JSON。 */
const char* engine_ocrFindText(
        const char* name,
        const char* imagePath,
        const char* text,
        const char* optionsJson
);

/** 返回最近一次 RapidOCR C ABI 失败原因。 */
const char* engine_ocrLastError();

/** 替换指定索引的自定义点阵字库。 */
int engine_fontSetDict(int index, const char* dictionary);

/** 向指定索引的自定义点阵字库追加字形。 */
int engine_fontAddDict(int index, const char* dictionary);

/** 为当前线程选择要使用的自定义点阵字库。 */
int engine_fontUseDict(int index);

/** 从当前截图生成 "宽$高$十六进制点阵" 字形描述。 */
const char* engine_fontGetPixel(int x1, int y1, int x2, int y2, const char* color);

/** 在当前截图区域内按当前字库识字，返回 text/items JSON。 */
const char* engine_fontOcr(
        int x1,
        int y1,
        int x2,
        int y2,
        const char* color,
        double sim
);

/** 在当前截图区域内查找文字。 */
int engine_fontFindStr(
        int x1,
        int y1,
        int x2,
        int y2,
        const char* text,
        const char* color,
        double sim,
        EnginePoint* point
);

/** 在当前截图区域内查找全部文字命中，返回坐标数组 JSON。 */
const char* engine_fontFindStrEx(
        int x1,
        int y1,
        int x2,
        int y2,
        const char* text,
        const char* color,
        double sim
);

/** 只识别目标文字涉及的字形并返回第一个命中，适合大字库固定文本查找。 */
int engine_fontFindStrFast(
        int x1,
        int y1,
        int x2,
        int y2,
        const char* text,
        const char* color,
        double sim,
        EnginePoint* point
);

/** 只识别目标文字涉及的字形并返回全部命中坐标 JSON 数组。 */
const char* engine_fontFindStrFastEx(
        int x1,
        int y1,
        int x2,
        int y2,
        const char* text,
        const char* color,
        double sim
);

/** 返回最近一次自定义字库 C ABI 失败原因。 */
const char* engine_fontLastError();

/**
 * 按住不放。
 *
 * id 为模拟手指索引，范围 0 到 4。该函数只走 Root helper 输入注入，不走无障碍。
 * 返回 1 表示 root helper 注入成功，返回 0 表示失败。
 */
int engine_touchDown(int id, int x, int y);

/**
 * 移动已按下的模拟手指。
 */
int engine_touchMove(int id, int x, int y);

/**
 * 弹起模拟手指。
 */
int engine_touchUp(int id);

/**
 * 按下一个按键不弹起。
 *
 * keyCode 支持数字字符串和常用按键标识符，例如 Home、Back、VolUp。
 */
int engine_keyDown(const char* keyCode);

/**
 * 弹起一个按键。
 */
int engine_keyUp(const char* keyCode);

/**
 * 按一下按键并弹起。
 */
int engine_keyPress(const char* keyCode);

/**
 * 模拟输入文字。
 *
 * 当前通过 Root 注入 KeyEvent 实现，适合英文、数字和常见符号。
 */
int engine_inputText(const char* text);

/**
 * 锁定 小鱼精灵 输入法。
 *
 * 成功时保存原默认输入法并切换到本应用输入法。该操作只走 Root helper，不走其他路线。
 */
int engine_imeLock();

/**
 * 通过已锁定的 小鱼精灵 输入法提交完整 Unicode 文本。
 */
int engine_imeSetText(const char* text);

/**
 * 恢复 imeLock 前保存的系统默认输入法。
 */
int engine_imeUnlock();

/**
 * 返回最近一次输入法 C ABI 失败原因。
 */
const char* engine_imeLastError();

/**
 * 返回当前运行环境类型。
 *
 * 当前输入注入只认 root；没有可用 Root helper 时返回 none。
 */
const char* engine_getRunEnvType();

/**
 * 返回最近一次输入 C ABI 失败原因。
 */
const char* engine_inputLastError();

/**
 * 创建脚本 UI 会话。
 *
 * surface 支持 dialog、hud、web；specJson 是完整 JSON 对象。成功返回大于 0 的
 * 会话 ID，失败返回 0，并可通过 engine_uiLastError() 获取原因。
 */
long long engine_uiOpen(const char* surface, const char* specJson);

/**
 * 更新 HUD 会话配置。
 *
 * 当前只有 hud 支持更新；其他界面类型返回 0。
 */
int engine_uiUpdate(long long sessionId, const char* specJson);

/**
 * 向 HTML 页面发送 JSON 消息。
 */
int engine_uiPostMessage(long long sessionId, const char* messageJson);

/**
 * 关闭一个脚本 UI 会话。
 */
int engine_uiClose(long long sessionId);

/**
 * 等待 UI 事件。
 *
 * 成功返回由 libengine.so 持有的 JSON：{"type":...,"data":...}；timeoutMs 小于
 * 0 时无限等待。失败返回空字符串，原因通过 engine_uiLastError() 获取。
 */
const char* engine_uiWaitEvent(long long sessionId, int timeoutMs);

/**
 * 带脚本停止检查的 UI 事件等待入口。
 *
 * Lua 等脚本运行时应使用该函数，确保弹窗或网页等待期间仍然可以停止脚本。
 */
const char* engine_uiWaitEventInterruptible(
        long long sessionId,
        int timeoutMs,
        runtime_interrupt_callback shouldInterrupt,
        void* userData
);

/**
 * 关闭当前脚本产生的全部 UI 会话。
 */
void engine_uiCloseAll();

/**
 * 返回当前线程最近一次 UI C ABI 失败原因。
 */
const char* engine_uiLastError();

#ifdef __cplusplus
}
#endif
