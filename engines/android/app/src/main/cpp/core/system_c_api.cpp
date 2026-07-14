/**
 * 文件用途：实现稳定 C ABI 门面，把外部调用转发到 core/api 统一核心实现。
 */
#include "system_c_api.h"

#include <string>
#include <map>
#include <vector>

#include "api/color_api.h"
#include "api/device_api.h"
#include "api/ime_api.h"
#include "api/input_api.h"
#include "api/package_api.h"
#include "api/runtime_api.h"
#include "api/screen_api.h"
#include "api/ui_api.h"
#include "../engine/engine_config.h"

namespace {

// 12 新增独立 EngineDeviceApi 函数表，旧版插件必须按新头文件重编译。
constexpr int kEngineAbiVersion = 12;
constexpr unsigned char kEmptyAlpkgResourceData = 0;

// 对外暴露当前 native 能力边界，方便 IDE、插件或脚本运行时确认可用能力。
constexpr const char* kCapabilitiesJson =
        "{"
        "\"abiVersion\":\"0.12\","
        "\"library\":\"libengine.so\","
        "\"core\":\"core/api + system_c_api\","
        "\"platform\":\"android\","
        "\"scriptBindings\":[\"lua\",\"js-reserved\",\"go-reserved\",\"plugin-reserved\"],"
        "\"pluginApi\":\"engine_getApi\","
        "\"runtimeApi\":[\"engine_print\",\"engine_logPrint\",\"engine_sleep\",\"engine_systemTime\",\"engine_tickCount\"],"
        "\"screenCapture\":[\"engine_capture\",\"engine_keepCapture\",\"engine_releaseCapture\",\"engine_setCaptureCacheMs\"],"
        "\"colorApi\":[\"engine_findColors\"],"
        "\"inputApi\":[\"engine_touchDown\",\"engine_touchMove\",\"engine_touchUp\",\"engine_keyDown\",\"engine_keyUp\",\"engine_keyPress\",\"engine_inputText\"],"
        "\"imeApi\":[\"engine_imeLock\",\"engine_imeSetText\",\"engine_imeUnlock\"],"
        "\"uiApi\":[\"engine_uiOpen\",\"engine_uiUpdate\",\"engine_uiPostMessage\",\"engine_uiClose\",\"engine_uiWaitEvent\"],"
        "\"deviceApi\":[\"engine_getDeviceApi\",\"engine_appIsFront\",\"engine_exec\",\"engine_getDisplayInfoJson\"],"
        "\"alpkgApi\":[\"engine_readAlpkgFile\"],"
        "\"imageFormat\":\"rgba8888\""
        "}";

thread_local std::string gRuntimeLastError;
thread_local std::string gScreenLastError;
thread_local std::string gColorLastError;
thread_local std::string gInputLastError;
thread_local std::string gImeLastError;
thread_local std::string gUiLastError;
thread_local std::string gUiEventResult;
thread_local std::string gDeviceLastError;
thread_local std::string gDeviceResult;
// C ABI 不能把 std::vector 暴露给调用方，因此由当前线程暂存最后一次包资源读取结果。
// Lua/JS/Go 绑定必须在返回到各自运行时前自行复制需要的内容。
thread_local std::vector<unsigned char> gAlpkgResourceData;

struct CInterruptContext {
    runtime_interrupt_callback callback = nullptr;
    void* userData = nullptr;
};

bool setRuntimeError(const std::string& error) {
    gRuntimeLastError = error;
    return false;
}

bool cInterruptAdapter(void* context) {
    auto* interrupt = static_cast<CInterruptContext*>(context);
    return interrupt != nullptr
            && interrupt->callback != nullptr
            && interrupt->callback(interrupt->userData) != 0;
}

/** 构造固定设备 API 使用的 JSON 对象参数，避免字符串拼接破坏中文或引号。 */
JsonValue deviceArguments(std::initializer_list<std::pair<std::string, JsonValue>> values = {}) {
    std::map<std::string, JsonValue> object;
    for (const auto& value : values) {
        object.emplace(value.first, value.second);
    }
    return JsonValue::makeObject(std::move(object));
}

/**
 * 调用 core/api/device_api，并将设备失败原因写入当前 C ABI 调用线程。
 */
bool callDevice(
        const char* operation,
        const JsonValue& arguments,
        JsonValue* value
) {
    std::string error;
    if (!xiaoyv::api::callDeviceApi(operation == nullptr ? "" : operation, arguments, value, &error)) {
        gDeviceLastError = error.empty() ? "设备 API 调用失败" : error;
        gDeviceResult.clear();
        return false;
    }
    gDeviceLastError.clear();
    return true;
}

/** 读取 Java 平台返回的布尔值。false 既可能是业务结果，也可能由调用者结合 lastError 判断。 */
int deviceBooleanResult(const char* operation, const JsonValue& arguments) {
    JsonValue value;
    if (!callDevice(operation, arguments, &value)) {
        return 0;
    }
    if (!value.isBool()) {
        gDeviceLastError = "设备 API 返回值不是布尔类型";
        return 0;
    }
    return value.boolValue() ? 1 : 0;
}

/** 读取 Java 平台返回的整数值。 */
int deviceIntegerResult(const char* operation, const JsonValue& arguments, int failureValue = -1) {
    JsonValue value;
    if (!callDevice(operation, arguments, &value)) {
        return failureValue;
    }
    if (!value.isNumber()) {
        gDeviceLastError = "设备 API 返回值不是整数类型";
        return failureValue;
    }
    return value.intValue(failureValue);
}

/**
 * 读取 Java 平台返回的字符串。null 表示该设备没有公开对应数据，返回 nullptr 而不是伪造值。
 */
const char* deviceStringResult(const char* operation, const JsonValue& arguments) {
    JsonValue value;
    if (!callDevice(operation, arguments, &value)) {
        return nullptr;
    }
    if (value.isNull()) {
        gDeviceResult.clear();
        return nullptr;
    }
    if (!value.isString()) {
        gDeviceLastError = "设备 API 返回值不是字符串类型";
        gDeviceResult.clear();
        return nullptr;
    }
    gDeviceResult = value.stringValue();
    return gDeviceResult.c_str();
}

/** 读取 Java 平台返回的对象或数组，并以 JSON 文本供 C ABI 外部调用方消费。 */
const char* deviceJsonResult(const char* operation, const JsonValue& arguments) {
    JsonValue value;
    if (!callDevice(operation, arguments, &value)) {
        return nullptr;
    }
    gDeviceResult = jsonValueToString(value);
    return gDeviceResult.c_str();
}

/** 执行没有 Lua 返回值的设备命令。 */
int deviceActionResult(const char* operation, const JsonValue& arguments) {
    JsonValue ignored;
    return callDevice(operation, arguments, &ignored) ? 1 : 0;
}

const EngineDeviceApi kEngineDeviceApi = {
        kEngineAbiVersion,
        engine_appIsFront,
        engine_appIsRunning,
        engine_frontAppName,
        engine_getCurrentActivity,
        engine_runApp,
        engine_stopApp,
        engine_runIntent,
        engine_installApk,
        engine_getInstalledApkJson,
        engine_getInstalledAppsJson,
        engine_getInsallAppInfosJson,
        engine_getApkVerInt,
        engine_exec,
        engine_exitScript,
        engine_getBatteryLevel,
        engine_getBoard,
        engine_getBootLoader,
        engine_getBrand,
        engine_getCpuAbi,
        engine_getCpuAbi2,
        engine_getCpuArch,
        engine_getDevice,
        engine_getDeviceId,
        engine_getDisplayDpi,
        engine_getDisplayInfoJson,
        engine_getDisplayRotate,
        engine_getDisplaySize,
        engine_getFingerprint,
        engine_getHardware,
        engine_getId,
        engine_getManufacturer,
        engine_getModel,
        engine_getNetWorkTime,
        engine_getOaid,
        engine_getOsVersionName,
        engine_getPackageName,
        engine_getProduct,
        engine_getRunEnvTypeCode,
        engine_getSdPath,
        engine_getSdkVersion,
        engine_getSensorsInfoJson,
        engine_getSimSerialNumber,
        engine_getSubscriberId,
        engine_getWifiMac,
        engine_getWorkPath,
        engine_lockScreen,
        engine_unLockScreen,
        engine_setDisplayPowerOff,
        engine_setAirplaneMode,
        engine_setBTEnable,
        engine_setWifiEnable,
        engine_phoneCall,
        engine_sendSms,
        engine_vibrate,
        engine_deviceLastError
};

const EngineApi kEngineApi = {
        kEngineAbiVersion,
        engine_getVersion,
        engine_getCapabilitiesJson,
        engine_print,
        engine_logPrint,
        engine_sleep,
        engine_sleepInterruptible,
        engine_systemTime,
        engine_tickCount,
        engine_runtimeLastError,
        engine_capture,
        engine_keepCapture,
        engine_releaseCapture,
        engine_setCaptureCacheMs,
        engine_captureLastError,
        engine_findColors,
        engine_findColorsLastError,
        engine_touchDown,
        engine_touchMove,
        engine_touchUp,
        engine_keyDown,
        engine_keyUp,
        engine_keyPress,
        engine_inputText,
        engine_getRunEnvType,
        engine_inputLastError,
        engine_imeLock,
        engine_imeSetText,
        engine_imeUnlock,
        engine_imeLastError,
        engine_uiOpen,
        engine_uiUpdate,
        engine_uiPostMessage,
        engine_uiClose,
        engine_uiWaitEvent,
        engine_uiWaitEventInterruptible,
        engine_uiCloseAll,
        engine_uiLastError,
        engine_readAlpkgFile,
        engine_getDeviceApi
};

} // namespace

/**
 * 返回引擎版本号。
 *
 * 返回值由 libengine.so 内部持有，调用方只读，不要释放。
 */
extern "C" const char* engine_getVersion() {
    return EngineConfig::kEngineVersion;
}

/**
 * 返回当前 C ABI 能力描述 JSON。
 *
 * 这个接口用于外部调用方确认当前 so 支持哪些稳定能力，不参与脚本运行逻辑。
 */
extern "C" const char* engine_getCapabilitiesJson() {
    return kCapabilitiesJson;
}

/**
 * 返回外部插件 so 使用的引擎函数表。
 *
 * 这和旧项目 setLrApi 的思路一致，区别是这里由宿主主动导出完整函数表。
 */
extern "C" const EngineApi* engine_getApi() {
    return &kEngineApi;
}

/**
 * 返回设备能力子函数表。
 *
 * 外部插件先调用 engine_getApi()，再通过 getDeviceApi() 取得该表；单独导出此符号是为
 * 了让只需要设备能力的 JS/Go 绑定无需解析整个顶层函数表。
 */
extern "C" const EngineDeviceApi* engine_getDeviceApi() {
    return &kEngineDeviceApi;
}

/**
 * 输出普通脚本日志。
 *
 * Lua 的 print、后续 JS/Go 的同类输出都应该进入这个 C ABI，再由 runtime_api
 * 写入 Android logcat 和 native 日志缓冲。
 */
extern "C" int engine_print(const char* text) {
    xiaoyv::api::runtimePrint(text == nullptr ? "" : text);
    gRuntimeLastError.clear();
    return 1;
}

/**
 * 输出日志模块文本。
 *
 * 当前和 engine_print 同级别输出，保留独立入口方便后续区分 print 与 log。
 */
extern "C" int engine_logPrint(const char* text) {
    xiaoyv::api::runtimeLogPrint(text == nullptr ? "" : text);
    gRuntimeLastError.clear();
    return 1;
}

/**
 * 不带中断检查的睡眠。
 *
 * 适合没有脚本停止上下文的外部调用方；脚本运行时应优先使用
 * engine_sleepInterruptible。
 */
extern "C" int engine_sleep(int durationMs) {
    return engine_sleepInterruptible(durationMs, nullptr, nullptr);
}

/**
 * 可中断睡眠。
 *
 * 语言绑定层传入 shouldInterrupt 回调，核心 runtime_api 只关心是否需要停止，
 * 不依赖具体脚本运行时类型。
 */
extern "C" int engine_sleepInterruptible(
        int durationMs,
        runtime_interrupt_callback shouldInterrupt,
        void* userData
) {
    if (durationMs < 0) {
        // lastError 会直接返回给 Lua、JS、Go 和插件调用方，统一使用中文说明参数错误。
        setRuntimeError("休眠时间必须大于等于 0 毫秒");
        return 0;
    }

    CInterruptContext interrupt{shouldInterrupt, userData};
    bool ok = xiaoyv::api::runtimeSleep(
            durationMs,
            shouldInterrupt == nullptr ? nullptr : cInterruptAdapter,
            shouldInterrupt == nullptr ? nullptr : &interrupt
    );

    if (!ok) {
        setRuntimeError("脚本已停止");
        return 0;
    }

    gRuntimeLastError.clear();
    return 1;
}

/**
 * 返回最近一次运行时 C ABI 失败原因。
 */
extern "C" const char* engine_runtimeLastError() {
    return gRuntimeLastError.c_str();
}

/**
 * 读取当前脚本包中由 manifest 声明的普通资源。
 *
 * 真实的 ZIP 索引、路径校验和资源类型校验集中在 core/api/package_api。这里仅承担
 * 稳定 C ABI 的内存所有权约定，供 Lua、未来 JS/Go 和插件使用同一份实现。
 */
extern "C" int engine_readAlpkgFile(
        const char* relativePath,
        const unsigned char** data,
        size_t* size
) {
    // 即使调用方只传错了一个输出参数，也不能让另一个参数保留上次成功读取的地址或
    // 长度。先逐个清零，再统一返回参数错误，保持 C ABI 的失败结果可预测。
    if (data != nullptr) {
        *data = nullptr;
    }
    if (size != nullptr) {
        *size = 0;
    }
    gAlpkgResourceData.clear();

    if (data == nullptr || size == nullptr) {
        setRuntimeError("ALPKG 资源读取输出参数为空");
        return 0;
    }

    if (relativePath == nullptr || relativePath[0] == '\0') {
        setRuntimeError("ALPKG 资源路径不能为空");
        return 0;
    }

    std::string error;
    if (!xiaoyv::api::readActiveAlpkgResource(relativePath, &gAlpkgResourceData, &error)) {
        setRuntimeError(error.empty() ? "读取 ALPKG 资源失败" : error);
        return 0;
    }

    // 空资源同样是读取成功。使用稳定的非空哨兵避免调用方把空内容误判为失败。
    *data = gAlpkgResourceData.empty() ? &kEmptyAlpkgResourceData : gAlpkgResourceData.data();
    *size = gAlpkgResourceData.size();
    gRuntimeLastError.clear();
    return 1;
}

/** 判断指定应用是否处于前台。 */
extern "C" int engine_appIsFront(const char* packageName) {
    return deviceBooleanResult(
            "app.isFront",
            deviceArguments({
                    {"packageName", JsonValue::makeString(packageName == nullptr ? "" : packageName)}
            })
    );
}

/** 判断指定应用主进程是否正在运行。 */
extern "C" int engine_appIsRunning(const char* packageName) {
    return deviceBooleanResult(
            "app.isRunning",
            deviceArguments({
                    {"packageName", JsonValue::makeString(packageName == nullptr ? "" : packageName)}
            })
    );
}

/** 返回当前前台应用包名；当前没有可识别前台组件时返回 nullptr。 */
extern "C" const char* engine_frontAppName() {
    return deviceStringResult("app.frontName", deviceArguments());
}

/** 返回当前前台 Activity 完整类名；读取失败或没有前台组件时返回 nullptr。 */
extern "C" const char* engine_getCurrentActivity() {
    return deviceStringResult("app.currentActivity", deviceArguments());
}

/**
 * Root 启动应用。
 *
 * componentName 为空时自动打开启动入口；isOpenBySuper 为兼容参数，当前 Root 引擎始终
 * 通过 RootDaemon 启动应用。
 */
extern "C" int engine_runApp(
        const char* packageName,
        const char* componentName,
        int isOpenBySuper
) {
    return deviceActionResult(
            "app.run",
            deviceArguments({
                    {"packageName", JsonValue::makeString(packageName == nullptr ? "" : packageName)},
                    {"componentName", componentName == nullptr || componentName[0] == '\0'
                            ? JsonValue::makeNull()
                            : JsonValue::makeString(componentName)},
                    {"isOpenBySuper", JsonValue::makeBool(isOpenBySuper != 0)}
            })
    );
}

/** Root 强制停止指定应用。 */
extern "C" int engine_stopApp(const char* packageName) {
    return deviceActionResult(
            "app.stop",
            deviceArguments({
                    {"packageName", JsonValue::makeString(packageName == nullptr ? "" : packageName)}
            })
    );
}

/** 用 JSON 对象描述 Android Intent 并请求打开。 */
extern "C" int engine_runIntent(const char* intentJson) {
    JsonValue arguments;
    std::string parseError;
    if (intentJson == nullptr
            || !parseJsonText(intentJson, &arguments, &parseError)
            || !arguments.isObject()) {
        gDeviceLastError = "Intent 参数必须是 JSON 对象";
        return 0;
    }
    return deviceBooleanResult("app.runIntent", arguments);
}

/** 通过 RootDaemon 安装指定绝对路径的 APK。 */
extern "C" int engine_installApk(const char* apkPath) {
    return deviceActionResult(
            "app.install",
            deviceArguments({
                    {"apkPath", JsonValue::makeString(apkPath == nullptr ? "" : apkPath)}
            })
    );
}

/** 返回安装 APK 绝对路径数组的 JSON。 */
extern "C" const char* engine_getInstalledApkJson() {
    return deviceJsonResult("app.installedApk", deviceArguments());
}

/** 返回已安装包名数组的 JSON。 */
extern "C" const char* engine_getInstalledAppsJson() {
    return deviceJsonResult("app.installedApps", deviceArguments());
}

/** 返回已安装应用详细信息数组的 JSON。 */
extern "C" const char* engine_getInsallAppInfosJson() {
    return deviceJsonResult("app.insallAppInfos", deviceArguments());
}

/** 返回当前 小鱼精灵 APK 的 versionCode。 */
extern "C" int engine_getApkVerInt() {
    return deviceIntegerResult("app.versionCode", deviceArguments(), 0);
}

/**
 * 执行 Root shell 命令。
 *
 * 该 C ABI 只负责把 RootDaemon 的文本输出交还调用方，不根据退出码改变结果。isRet 为 0
 * 时仍执行命令，但清空返回文本，保持懒人同名 API 的“无需结果”语义。
 */
extern "C" const char* engine_exec(const char* command, int isRet) {
    JsonValue value;
    if (!callDevice(
            "system.exec",
            deviceArguments({
                    {"command", JsonValue::makeString(command == nullptr ? "" : command)}
            }),
            &value
    )) {
        return nullptr;
    }
    if (!value.isString()) {
        gDeviceLastError = "命令执行返回值不是字符串类型";
        return nullptr;
    }
    gDeviceResult = isRet != 0 ? value.stringValue() : "";
    return gDeviceResult.c_str();
}

/** 请求停止当前顶层脚本。 */
extern "C" int engine_exitScript() {
    if (!xiaoyv::api::runtimeRequestScriptStop()) {
        gDeviceLastError = "当前没有可停止的脚本";
        return 0;
    }
    gDeviceLastError.clear();
    return 1;
}

/** 返回设备电量百分比；无法读取时返回 -1。 */
extern "C" int engine_getBatteryLevel() {
    return deviceIntegerResult("device.batteryLevel", deviceArguments());
}

extern "C" const char* engine_getBoard() {
    return deviceStringResult("device.board", deviceArguments());
}

extern "C" const char* engine_getBootLoader() {
    return deviceStringResult("device.bootLoader", deviceArguments());
}

extern "C" const char* engine_getBrand() {
    return deviceStringResult("device.brand", deviceArguments());
}

extern "C" const char* engine_getCpuAbi() {
    return deviceStringResult("device.cpuAbi", deviceArguments());
}

extern "C" const char* engine_getCpuAbi2() {
    return deviceStringResult("device.cpuAbi2", deviceArguments());
}

extern "C" int engine_getCpuArch() {
    return deviceIntegerResult("device.cpuArch", deviceArguments());
}

extern "C" const char* engine_getDevice() {
    return deviceStringResult("device.device", deviceArguments());
}

extern "C" const char* engine_getDeviceId() {
    return deviceStringResult("device.deviceId", deviceArguments());
}

extern "C" int engine_getDisplayDpi() {
    return deviceIntegerResult("device.displayDpi", deviceArguments());
}

/** 返回屏幕宽高、DPI、密度和旋转方向的 JSON 对象。 */
extern "C" const char* engine_getDisplayInfoJson() {
    return deviceJsonResult("device.displayInfo", deviceArguments());
}

extern "C" int engine_getDisplayRotate() {
    return deviceIntegerResult("device.displayRotate", deviceArguments());
}

/** 返回真实显示宽高，失败时两个输出参数都会清零。 */
extern "C" int engine_getDisplaySize(int* width, int* height) {
    if (width != nullptr) {
        *width = 0;
    }
    if (height != nullptr) {
        *height = 0;
    }
    if (width == nullptr || height == nullptr) {
        gDeviceLastError = "屏幕尺寸输出参数不能为空";
        return 0;
    }

    JsonValue value;
    if (!callDevice("device.displaySize", deviceArguments(), &value)) {
        return 0;
    }
    const JsonValue* widthValue = value.get("width");
    const JsonValue* heightValue = value.get("height");
    if (!value.isObject()
            || widthValue == nullptr
            || heightValue == nullptr
            || !widthValue->isNumber()
            || !heightValue->isNumber()) {
        gDeviceLastError = "屏幕尺寸返回值无效";
        return 0;
    }

    *width = widthValue->intValue();
    *height = heightValue->intValue();
    if (*width <= 0 || *height <= 0) {
        *width = 0;
        *height = 0;
        gDeviceLastError = "屏幕尺寸无效";
        return 0;
    }
    gDeviceLastError.clear();
    return 1;
}

extern "C" const char* engine_getFingerprint() {
    return deviceStringResult("device.fingerprint", deviceArguments());
}

extern "C" const char* engine_getHardware() {
    return deviceStringResult("device.hardware", deviceArguments());
}

extern "C" const char* engine_getId() {
    return deviceStringResult("device.id", deviceArguments());
}

extern "C" const char* engine_getManufacturer() {
    return deviceStringResult("device.manufacturer", deviceArguments());
}

extern "C" const char* engine_getModel() {
    return deviceStringResult("device.model", deviceArguments());
}

extern "C" const char* engine_getNetWorkTime() {
    return deviceStringResult("device.networkTime", deviceArguments());
}

extern "C" const char* engine_getOaid() {
    return deviceStringResult("device.oaid", deviceArguments());
}

extern "C" const char* engine_getOsVersionName() {
    return deviceStringResult("device.osVersionName", deviceArguments());
}

extern "C" const char* engine_getPackageName() {
    return deviceStringResult("app.packageName", deviceArguments());
}

extern "C" const char* engine_getProduct() {
    return deviceStringResult("device.product", deviceArguments());
}

/** 返回 0（Root）、1（无障碍）或 -1（当前没有可用运行环境）。 */
extern "C" int engine_getRunEnvTypeCode() {
    return deviceIntegerResult("device.runEnvType", deviceArguments());
}

extern "C" const char* engine_getSdPath() {
    return deviceStringResult("device.sdPath", deviceArguments());
}

extern "C" int engine_getSdkVersion() {
    return deviceIntegerResult("device.sdkVersion", deviceArguments());
}

extern "C" const char* engine_getSensorsInfoJson() {
    return deviceJsonResult("device.sensorsInfo", deviceArguments());
}

extern "C" const char* engine_getSimSerialNumber() {
    return deviceStringResult("device.simSerialNumber", deviceArguments());
}

extern "C" const char* engine_getSubscriberId() {
    return deviceStringResult("device.subscriberId", deviceArguments());
}

extern "C" const char* engine_getWifiMac() {
    return deviceStringResult("device.wifiMac", deviceArguments());
}

/** 返回当前脚本文件或 ALPKG 所在目录。 */
extern "C" const char* engine_getWorkPath() {
    gDeviceResult = xiaoyv::api::runtimeScriptWorkPath();
    gDeviceLastError.clear();
    return gDeviceResult.empty() ? nullptr : gDeviceResult.c_str();
}

/** 保持屏幕常亮，不会锁定设备。 */
extern "C" int engine_lockScreen() {
    return deviceActionResult("system.keepAwake", deviceArguments());
}

/** 释放 lockScreen 获取的屏幕常亮锁。 */
extern "C" int engine_unLockScreen() {
    return deviceActionResult("system.releaseAwake", deviceArguments());
}

extern "C" int engine_setDisplayPowerOff(int isPowerOff) {
    return deviceActionResult(
            "system.displayPower",
            deviceArguments({{"powerOff", JsonValue::makeBool(isPowerOff != 0)}})
    );
}

extern "C" int engine_setAirplaneMode(int enabled) {
    return deviceActionResult(
            "system.airplane",
            deviceArguments({{"enabled", JsonValue::makeBool(enabled != 0)}})
    );
}

extern "C" int engine_setBTEnable(int enabled) {
    return deviceActionResult(
            "system.bluetooth",
            deviceArguments({{"enabled", JsonValue::makeBool(enabled != 0)}})
    );
}

extern "C" int engine_setWifiEnable(int enabled) {
    return deviceActionResult(
            "system.wifi",
            deviceArguments({{"enabled", JsonValue::makeBool(enabled != 0)}})
    );
}

extern "C" int engine_phoneCall(const char* number, int state) {
    return deviceActionResult(
            "system.phoneCall",
            deviceArguments({
                    {"number", JsonValue::makeString(number == nullptr ? "" : number)},
                    {"state", JsonValue::makeNumber(state)}
            })
    );
}

extern "C" int engine_sendSms(const char* number, const char* content) {
    return deviceActionResult(
            "system.sendSms",
            deviceArguments({
                    {"number", JsonValue::makeString(number == nullptr ? "" : number)},
                    {"content", JsonValue::makeString(content == nullptr ? "" : content)}
            })
    );
}

extern "C" int engine_vibrate(int durationMs) {
    if (durationMs < 0) {
        gDeviceLastError = "震动时间必须大于等于 0 毫秒";
        return 0;
    }
    return deviceActionResult(
            "system.vibrate",
            deviceArguments({{"durationMs", JsonValue::makeNumber(durationMs)}})
    );
}

/** 返回当前线程最近一次设备 API 失败原因。 */
extern "C" const char* engine_deviceLastError() {
    return gDeviceLastError.c_str();
}

/**
 * 返回系统时间戳，单位毫秒。
 *
 * 这里直接转发到 runtime_api，保持 Lua/JS/Go/插件看到同一套时间语义。
 */
extern "C" long long engine_systemTime() {
    return xiaoyv::api::runtimeSystemTimeMs();
}

/**
 * 返回当前脚本运行时间，单位毫秒。
 *
 * 起点由对应脚本运行时在顶层任务开始前记录，当前 Lua 主任务和全部子线程共享。
 */
extern "C" long long engine_tickCount() {
    return xiaoyv::api::runtimeTickCountMs();
}

/**
 * 获取屏幕截图。
 *
 * 成功时写出 width、height 和 pixels；pixels 指向内部缓存，格式固定为紧凑 RGBA。
 * 缓存、锁帧和 Root 截图分发都在 screen_api 中完成。
 */
extern "C" int engine_capture(int* width, int* height, unsigned char** pixels) {
    if (width == nullptr || height == nullptr || pixels == nullptr) {
        gScreenLastError = "截图输出指针不能为空";
        return 0;
    }

    xiaoyv::api::ScreenFrame frame;
    if (!xiaoyv::api::captureScreen(&frame)) {
        *width = 0;
        *height = 0;
        *pixels = nullptr;
        gScreenLastError = xiaoyv::api::screenLastError();
        return 0;
    }

    *width = frame.width;
    *height = frame.height;
    *pixels = frame.pixels;
    gScreenLastError.clear();
    return 1;
}

/**
 * 开启锁帧。
 */
extern "C" void engine_keepCapture() {
    xiaoyv::api::keepScreenCapture();
}

/**
 * 取消锁帧。
 */
extern "C" void engine_releaseCapture() {
    xiaoyv::api::releaseScreenCapture();
}

/**
 * 设置截图缓存时间，单位毫秒。
 */
extern "C" int engine_setCaptureCacheMs(int durationMs) {
    if (!xiaoyv::api::setScreenCaptureCacheMs(durationMs)) {
        gScreenLastError = xiaoyv::api::screenLastError();
        return 0;
    }

    gScreenLastError.clear();
    return 1;
}

/**
 * 返回最近一次截图 C ABI 失败原因。
 */
extern "C" const char* engine_captureLastError() {
    return gScreenLastError.c_str();
}

/**
 * 在当前屏幕截图缓存上执行多点找色。
 *
 * 找色核心内部会调用 screen_api，所以这里没有“是否截屏”参数；缓存策略统一由
 * engine_capture/engine_keepCapture/engine_releaseCapture/engine_setCaptureCacheMs 控制。
 */
extern "C" int engine_findColors(
        int x1,
        int y1,
        int x2,
        int y2,
        int dir,
        int sim,
        const char* colors,
        EnginePoint* point
) {
    if (point == nullptr) {
        gColorLastError = "多点找色输出坐标不能为空";
        return 0;
    }

    xiaoyv::api::找色坐标 colorPoint;
    bool found = xiaoyv::api::在屏幕中多点找色(
            x1,
            y1,
            x2,
            y2,
            dir,
            sim,
            colors,
            &colorPoint
    );

    point->x = colorPoint.x;
    point->y = colorPoint.y;
    if (!found) {
        gColorLastError = xiaoyv::api::取找色错误();
        return 0;
    }

    gColorLastError.clear();
    return 1;
}

/**
 * 返回最近一次找色 C ABI 失败原因。
 */
extern "C" const char* engine_findColorsLastError() {
    return gColorLastError.c_str();
}

/**
 * 按住不放。
 *
 * Lua 的 touchDown 和后续 JS/Go 的同类调用都走这里，再进入 input_api。
 */
extern "C" int engine_touchDown(int id, int x, int y) {
    if (!xiaoyv::api::inputTouchDown(id, x, y)) {
        gInputLastError = xiaoyv::api::inputLastError();
        return 0;
    }
    gInputLastError.clear();
    return 1;
}

/**
 * 移动已按下的模拟手指。
 */
extern "C" int engine_touchMove(int id, int x, int y) {
    if (!xiaoyv::api::inputTouchMove(id, x, y)) {
        gInputLastError = xiaoyv::api::inputLastError();
        return 0;
    }
    gInputLastError.clear();
    return 1;
}

/**
 * 弹起模拟手指。
 */
extern "C" int engine_touchUp(int id) {
    if (!xiaoyv::api::inputTouchUp(id)) {
        gInputLastError = xiaoyv::api::inputLastError();
        return 0;
    }
    gInputLastError.clear();
    return 1;
}

/**
 * 按下一个按键不弹起。
 */
extern "C" int engine_keyDown(const char* keyCode) {
    if (!xiaoyv::api::inputKeyDown(keyCode)) {
        gInputLastError = xiaoyv::api::inputLastError();
        return 0;
    }
    gInputLastError.clear();
    return 1;
}

/**
 * 弹起一个按键。
 */
extern "C" int engine_keyUp(const char* keyCode) {
    if (!xiaoyv::api::inputKeyUp(keyCode)) {
        gInputLastError = xiaoyv::api::inputLastError();
        return 0;
    }
    gInputLastError.clear();
    return 1;
}

/**
 * 按一下按键并弹起。
 */
extern "C" int engine_keyPress(const char* keyCode) {
    if (!xiaoyv::api::inputKeyPress(keyCode)) {
        gInputLastError = xiaoyv::api::inputLastError();
        return 0;
    }
    gInputLastError.clear();
    return 1;
}

/**
 * 模拟输入文字。
 */
extern "C" int engine_inputText(const char* text) {
    if (!xiaoyv::api::inputText(text)) {
        gInputLastError = xiaoyv::api::inputLastError();
        return 0;
    }
    gInputLastError.clear();
    return 1;
}

/**
 * 锁定 小鱼精灵 输入法。
 */
extern "C" int engine_imeLock() {
    if (!xiaoyv::api::imeLock()) {
        gImeLastError = xiaoyv::api::imeLastError();
        return 0;
    }
    gImeLastError.clear();
    return 1;
}

/**
 * 通过已锁定的 小鱼精灵 输入法提交完整 Unicode 文本。
 */
extern "C" int engine_imeSetText(const char* text) {
    if (!xiaoyv::api::imeSetText(text)) {
        gImeLastError = xiaoyv::api::imeLastError();
        return 0;
    }
    gImeLastError.clear();
    return 1;
}

/**
 * 恢复 imeLock 前保存的系统默认输入法。
 */
extern "C" int engine_imeUnlock() {
    if (!xiaoyv::api::imeUnlock()) {
        gImeLastError = xiaoyv::api::imeLastError();
        return 0;
    }
    gImeLastError.clear();
    return 1;
}

/**
 * 返回最近一次输入法 C ABI 失败原因。
 */
extern "C" const char* engine_imeLastError() {
    return gImeLastError.c_str();
}

/**
 * 返回当前运行环境类型。
 */
extern "C" const char* engine_getRunEnvType() {
    static thread_local std::string envType;
    envType = xiaoyv::api::getRunEnvType();
    return envType.c_str();
}

/**
 * 返回最近一次输入 C ABI 失败原因。
 */
extern "C" const char* engine_inputLastError() {
    return gInputLastError.c_str();
}

/**
 * 创建脚本 UI 会话。
 *
 * 固定 C ABI 只负责稳定参数和错误返回；会话表、等待队列和 Android UI 分发集中在
 * core/api/ui_api，因此 Lua、JS、Go 和插件不会各自维护一套界面状态。
 */
extern "C" long long engine_uiOpen(const char* surface, const char* specJson) {
    long long sessionId = 0;
    if (!xiaoyv::api::openUiSurface(
            surface == nullptr ? "" : surface,
            specJson == nullptr ? "{}" : specJson,
            &sessionId
    )) {
        gUiLastError = xiaoyv::api::uiLastError();
        return 0;
    }

    gUiLastError.clear();
    return sessionId;
}

/**
 * 更新脚本 UI 会话。
 */
extern "C" int engine_uiUpdate(long long sessionId, const char* specJson) {
    if (!xiaoyv::api::updateUiSurface(sessionId, specJson == nullptr ? "{}" : specJson)) {
        gUiLastError = xiaoyv::api::uiLastError();
        return 0;
    }

    gUiLastError.clear();
    return 1;
}

/**
 * 向 HTML 页面发送 JSON 消息。
 */
extern "C" int engine_uiPostMessage(long long sessionId, const char* messageJson) {
    if (!xiaoyv::api::postUiMessage(sessionId, messageJson == nullptr ? "null" : messageJson)) {
        gUiLastError = xiaoyv::api::uiLastError();
        return 0;
    }

    gUiLastError.clear();
    return 1;
}

/**
 * 关闭一个脚本 UI 会话。
 */
extern "C" int engine_uiClose(long long sessionId) {
    if (!xiaoyv::api::closeUiSurface(sessionId)) {
        gUiLastError = xiaoyv::api::uiLastError();
        return 0;
    }

    gUiLastError.clear();
    return 1;
}

/**
 * 无脚本中断上下文的 UI 事件等待入口。
 */
extern "C" const char* engine_uiWaitEvent(long long sessionId, int timeoutMs) {
    return engine_uiWaitEventInterruptible(sessionId, timeoutMs, nullptr, nullptr);
}

/**
 * 可中断的 UI 事件等待入口。
 */
extern "C" const char* engine_uiWaitEventInterruptible(
        long long sessionId,
        int timeoutMs,
        runtime_interrupt_callback shouldInterrupt,
        void* userData
) {
    CInterruptContext interrupt{shouldInterrupt, userData};
    if (!xiaoyv::api::waitUiEvent(
            sessionId,
            timeoutMs,
            shouldInterrupt == nullptr ? nullptr : cInterruptAdapter,
            shouldInterrupt == nullptr ? nullptr : &interrupt,
            &gUiEventResult
    )) {
        gUiLastError = xiaoyv::api::uiLastError();
        gUiEventResult.clear();
        return gUiEventResult.c_str();
    }

    gUiLastError.clear();
    return gUiEventResult.c_str();
}

/**
 * 关闭所有脚本 UI 会话。
 */
extern "C" void engine_uiCloseAll() {
    xiaoyv::api::closeAllUiSurfaces();
    gUiLastError.clear();
}

/**
 * 返回最近一次 UI C ABI 失败原因。
 */
extern "C" const char* engine_uiLastError() {
    return gUiLastError.c_str();
}
