/**
 * 文件用途：实现 Android 设备信息、应用管理和系统控制的唯一平台适配层。
 */
package com.xiaoyv.engine;

import android.Manifest;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.BatteryManager;
import android.os.Build;
import android.os.Environment;
import android.os.PowerManager;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;
import android.provider.Settings;
import android.telephony.SmsManager;
import android.telephony.TelephonyManager;
import android.util.DisplayMetrics;
import android.view.Display;
import android.view.WindowManager;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.net.NetworkInterface;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Date;
import java.util.Enumeration;
import java.util.List;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Android 平台设备能力实现。
 *
 * native 侧只会传入本类明确支持的 operation，所有 Lua/JS/Go 调用先进入
 * libengine.so/core/api，再由 AndroidHostBridge 转发到这里。这样 Java 不承担脚本 API
 * 定义，也不会成为另一套可被脚本任意调用的接口。
 *
 * 返回值统一采用 JSON 信封：{"ok":true,"value":...} 或
 * {"ok":false,"error":"..."}。数值、字符串、数组和对象都保留真实 JSON 类型，
 * native 可无损转换为 Lua 返回值或稳定 C ABI 结果。
 */
final class DevicePlatformBridge {
    private static final Pattern COMPONENT_PATTERN = Pattern.compile(
            "([A-Za-z][A-Za-z0-9_.$]*)/(\\.?[A-Za-z][A-Za-z0-9_.$]*)"
    );
    private static final Object WAKE_LOCK_LOCK = new Object();
    private static PowerManager.WakeLock screenWakeLock;

    private DevicePlatformBridge() {
    }

    /**
     * 分发一个由 native 固定定义的设备操作。
     *
     * argumentsJson 始终要求 JSON 对象，避免参数拼接进 shell 命令。需要 Root 的操作只
     * 调用常驻 RootDaemon 一次，不重新探测 su，也不进行无障碍或普通权限回退。
     */
    static String call(Context context, String operation, String argumentsJson) {
        if (context == null) {
            return failure("Android Application Context 不可用");
        }

        final JSONObject arguments;
        try {
            arguments = argumentsJson == null || argumentsJson.trim().isEmpty()
                    ? new JSONObject()
                    : new JSONObject(argumentsJson);
        } catch (JSONException exception) {
            return failure("设备 API 参数 JSON 无效");
        }

        try {
            Object value = dispatch(context, operation == null ? "" : operation, arguments);
            return success(value);
        } catch (IllegalArgumentException exception) {
            return failure(exception.getMessage());
        } catch (SecurityException exception) {
            return failure("系统权限不足：" + exception.getMessage());
        } catch (Exception exception) {
            return failure("设备操作失败：" + exception.getMessage());
        }
    }

    /**
     * 按能力分类处理，确保 operation 列表在一个文件内可审阅。
     */
    private static Object dispatch(Context context, String operation, JSONObject arguments)
            throws Exception {
        switch (operation) {
            case "app.isFront":
                return isAppFront(requireString(arguments, "packageName"));
            case "app.isRunning":
                return isAppRunning(requireString(arguments, "packageName"));
            case "app.frontName":
                return currentComponentPackage();
            case "app.currentActivity":
                return currentComponentActivity();
            case "app.packageName":
                return context.getPackageName();
            case "app.run":
                runApp(arguments);
                return JSONObject.NULL;
            case "app.stop":
                stopApp(requireString(arguments, "packageName"));
                return JSONObject.NULL;
            case "app.runIntent":
                return runIntent(context, arguments);
            case "app.install":
                installApk(requireString(arguments, "apkPath"));
                return JSONObject.NULL;
            case "app.installedApk":
                return installedApkPaths(context);
            case "app.installedApps":
                return installedPackageNames(context);
            case "app.insallAppInfos":
                return installedAppInfos(context);
            case "app.versionCode":
                return appVersionCode(context);

            case "system.exec":
                return executeRootCommand(requireString(arguments, "command"));
            case "system.keepAwake":
                keepScreenAwake(context);
                return JSONObject.NULL;
            case "system.releaseAwake":
                releaseScreenAwake();
                return JSONObject.NULL;
            case "system.displayPower":
                setDisplayPower(context, arguments.optBoolean("powerOff", false));
                return JSONObject.NULL;
            case "system.airplane":
                setAirplaneMode(arguments.optBoolean("enabled", false));
                return JSONObject.NULL;
            case "system.bluetooth":
                setBluetoothEnabled(arguments.optBoolean("enabled", false));
                return JSONObject.NULL;
            case "system.wifi":
                setWifiEnabled(arguments.optBoolean("enabled", false));
                return JSONObject.NULL;
            case "system.phoneCall":
                phoneCall(
                        requireString(arguments, "number"),
                        arguments.optInt("state", 0)
                );
                return JSONObject.NULL;
            case "system.sendSms":
                sendSms(context, requireString(arguments, "number"), requireString(arguments, "content"));
                return JSONObject.NULL;
            case "system.vibrate":
                vibrate(context, requireNonNegativeInt(arguments, "durationMs"));
                return JSONObject.NULL;

            case "device.batteryLevel":
                return batteryLevel(context);
            case "device.board":
                return Build.BOARD;
            case "device.bootLoader":
                return Build.BOOTLOADER;
            case "device.brand":
                return Build.BRAND;
            case "device.cpuAbi":
                return cpuAbi(0);
            case "device.cpuAbi2":
                return cpuAbi(1);
            case "device.cpuArch":
                return cpuArch();
            case "device.device":
                return Build.DEVICE;
            case "device.deviceId":
                return Settings.Secure.getString(
                        context.getContentResolver(),
                        Settings.Secure.ANDROID_ID
                );
            case "device.displayDpi":
                return displayMetrics(context).densityDpi;
            case "device.displayInfo":
                return displayInfo(context);
            case "device.displayRotate":
                return displayRotation(context);
            case "device.displaySize":
                return displaySize(context);
            case "device.fingerprint":
                return Build.FINGERPRINT;
            case "device.hardware":
                return Build.HARDWARE;
            case "device.id":
                return Build.ID;
            case "device.manufacturer":
                return Build.MANUFACTURER;
            case "device.model":
                return Build.MODEL;
            case "device.networkTime":
                return new SimpleDateFormat("yyyy-MM-dd_HH-mm-ss", Locale.getDefault())
                        .format(new Date());
            case "device.oaid":
                // OAID 不是 Android Framework 标准字段，不能把 ANDROID_ID 伪装成 OAID。
                return JSONObject.NULL;
            case "device.osVersionName":
                return Build.VERSION.RELEASE;
            case "device.product":
                return Build.PRODUCT;
            case "device.runEnvType":
                return runEnvironmentType(context);
            case "device.sdkVersion":
                return Build.VERSION.SDK_INT;
            case "device.sdPath":
                return Environment.getExternalStorageDirectory().getAbsolutePath();
            case "device.sensorsInfo":
                return sensorsInfo(context);
            case "device.simSerialNumber":
                return simSerialNumber(context);
            case "device.subscriberId":
                return subscriberId(context);
            case "device.wifiMac":
                return wifiMacAddress();
            default:
                throw new IllegalArgumentException("不支持的设备能力：" + operation);
        }
    }

    /**
     * 当前前台组件只能由 RootDaemon 查询。Android 10 以后普通应用不能可靠读取其他应用
     * 前台状态，因此这里不使用不稳定的 ActivityManager 回退路线。
     */
    private static String currentComponentPackage() {
        ComponentName component = currentFrontComponent();
        return component == null ? null : component.packageName;
    }

    private static String currentComponentActivity() {
        ComponentName component = currentFrontComponent();
        return component == null ? null : component.activityName;
    }

    private static boolean isAppFront(String packageName) {
        String currentPackage = currentComponentPackage();
        return packageName.equals(currentPackage);
    }

    private static boolean isAppRunning(String packageName) {
        String output = executeRootCommand("pidof " + shellQuote(packageName));
        return !output.trim().isEmpty();
    }

    private static ComponentName currentFrontComponent() {
        String output = executeRootCommand(
                "dumpsys window windows | grep -m 1 -E 'mCurrentFocus|mFocusedApp'"
        );
        Matcher matcher = COMPONENT_PATTERN.matcher(output);
        if (!matcher.find()) {
            return null;
        }

        String packageName = matcher.group(1);
        String activityName = matcher.group(2);
        if (activityName.startsWith(".")) {
            activityName = packageName + activityName;
        }
        return new ComponentName(packageName, activityName);
    }

    /**
     * 打开应用统一使用 RootDaemon。没有组件名时由 monkey 查找启动入口；传入组件名时由
     * am 精确打开组件。isOpenBySuper 保留为兼容参数，当前 Root 引擎始终在最高权限执行。
     */
    private static void runApp(JSONObject arguments) {
        String packageName = requireString(arguments, "packageName");
        String componentName = nullableString(arguments, "componentName");
        if (componentName == null) {
            executeRootCommand(
                    "monkey -p " + shellQuote(packageName)
                            + " -c android.intent.category.LAUNCHER 1"
            );
            return;
        }

        String component = componentName.contains("/")
                ? componentName
                : packageName + "/" + componentName;
        executeRootCommand("am start -n " + shellQuote(component));
    }

    private static void stopApp(String packageName) {
        executeRootCommand("am force-stop " + shellQuote(packageName));
    }

    /**
     * 用 Android Intent 实现结构化跳转。extra 支持 string、boolean、integer、number；
     * 其他复杂值会以 JSON 字符串传递，避免 silent 丢失脚本表数据。
     */
    private static boolean runIntent(Context context, JSONObject arguments) throws JSONException {
        String action = nullableString(arguments, "action");
        Intent intent = new Intent(action == null ? Intent.ACTION_VIEW : action);
        String uri = nullableString(arguments, "uri");
        String data = nullableString(arguments, "data");
        String target = data == null ? uri : data;
        if (target != null) {
            intent.setData(Uri.parse(target));
        }

        String packageName = nullableString(arguments, "packageName");
        if (packageName != null) {
            intent.setPackage(packageName);
        }

        JSONObject extra = arguments.optJSONObject("extra");
        if (extra != null) {
            addIntentExtras(intent, extra);
        }

        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        context.startActivity(intent);
        return true;
    }

    private static void addIntentExtras(Intent intent, JSONObject extra) throws JSONException {
        for (java.util.Iterator<String> iterator = extra.keys(); iterator.hasNext();) {
            String key = iterator.next();
            Object value = extra.get(key);
            if (value instanceof Boolean) {
                intent.putExtra(key, (Boolean) value);
            } else if (value instanceof Integer) {
                intent.putExtra(key, (Integer) value);
            } else if (value instanceof Long) {
                intent.putExtra(key, (Long) value);
            } else if (value instanceof Double) {
                intent.putExtra(key, (Double) value);
            } else if (value == JSONObject.NULL) {
                intent.putExtra(key, "");
            } else if (value instanceof String) {
                intent.putExtra(key, (String) value);
            } else {
                intent.putExtra(key, String.valueOf(value));
            }
        }
    }

    private static void installApk(String apkPath) {
        executeRootCommand("pm install -r " + shellQuote(apkPath));
    }

    private static JSONArray installedApkPaths(Context context) {
        JSONArray result = new JSONArray();
        for (PackageInfo packageInfo : installedPackages(context)) {
            if (packageInfo.applicationInfo != null
                    && packageInfo.applicationInfo.sourceDir != null) {
                result.put(packageInfo.applicationInfo.sourceDir);
            }
        }
        return result;
    }

    private static JSONArray installedPackageNames(Context context) {
        JSONArray result = new JSONArray();
        for (PackageInfo packageInfo : installedPackages(context)) {
            result.put(packageInfo.packageName);
        }
        return result;
    }

    private static JSONArray installedAppInfos(Context context) throws JSONException {
        JSONArray result = new JSONArray();
        PackageManager packageManager = context.getPackageManager();
        for (PackageInfo packageInfo : installedPackages(context)) {
            ApplicationInfo applicationInfo = packageInfo.applicationInfo;
            JSONObject item = new JSONObject();
            item.put("packageName", packageInfo.packageName);
            item.put("appName", applicationInfo == null
                    ? packageInfo.packageName
                    : String.valueOf(applicationInfo.loadLabel(packageManager)));
            item.put("versionName", packageInfo.versionName == null ? "" : packageInfo.versionName);
            item.put("versionCode", packageVersionCode(packageInfo));
            item.put("apkPath", applicationInfo == null ? "" : applicationInfo.sourceDir);
            item.put("systemApp", applicationInfo != null
                    && (applicationInfo.flags & ApplicationInfo.FLAG_SYSTEM) != 0);
            result.put(item);
        }
        return result;
    }

    @SuppressWarnings("deprecation")
    private static List<PackageInfo> installedPackages(Context context) {
        List<PackageInfo> packages = context.getPackageManager().getInstalledPackages(0);
        if (packages == null) {
            return Collections.emptyList();
        }
        List<PackageInfo> sorted = new ArrayList<>(packages);
        Collections.sort(sorted, (left, right) -> left.packageName.compareTo(right.packageName));
        return sorted;
    }

    private static long appVersionCode(Context context) throws PackageManager.NameNotFoundException {
        return packageVersionCode(context.getPackageManager().getPackageInfo(context.getPackageName(), 0));
    }

    @SuppressWarnings("deprecation")
    private static long packageVersionCode(PackageInfo packageInfo) {
        return Build.VERSION.SDK_INT >= Build.VERSION_CODES.P
                ? packageInfo.getLongVersionCode()
                : packageInfo.versionCode;
    }

    /**
     * Root exec 的返回只表示 RootDaemon 已执行该命令。命令退出码不参与判断，符合脚本
     * exec 的语义；调用者可以直接检查返回文本。
     */
    private static String executeRootCommand(String command) {
        RootHelperBridge.ShellResult result = RootHelperBridge.executeShell(command);
        if (!result.success) {
            throw new IllegalArgumentException(result.error);
        }
        return result.output;
    }

    private static void keepScreenAwake(Context context) {
        synchronized (WAKE_LOCK_LOCK) {
            if (screenWakeLock == null) {
                PowerManager powerManager = (PowerManager) context.getSystemService(Context.POWER_SERVICE);
                if (powerManager == null) {
                    throw new IllegalArgumentException("电源服务不可用");
                }
                // 该语义与懒人 lockScreen 一致：保持屏幕常亮，而不是锁定设备。
                screenWakeLock = powerManager.newWakeLock(
                        PowerManager.SCREEN_BRIGHT_WAKE_LOCK,
                        context.getPackageName() + ":script-screen-awake"
                );
                screenWakeLock.setReferenceCounted(false);
            }
            if (!screenWakeLock.isHeld()) {
                screenWakeLock.acquire();
            }
        }
    }

    private static void releaseScreenAwake() {
        synchronized (WAKE_LOCK_LOCK) {
            if (screenWakeLock != null && screenWakeLock.isHeld()) {
                screenWakeLock.release();
            }
        }
    }

    private static void setDisplayPower(Context context, boolean powerOff) {
        PowerManager powerManager = (PowerManager) context.getSystemService(Context.POWER_SERVICE);
        if (powerManager == null) {
            throw new IllegalArgumentException("电源服务不可用");
        }
        boolean shouldBeInteractive = !powerOff;
        if (powerManager.isInteractive() != shouldBeInteractive) {
            executeRootCommand("input keyevent 26");
        }
    }

    private static void setAirplaneMode(boolean enabled) {
        String state = enabled ? "1" : "0";
        String bool = enabled ? "true" : "false";
        executeRootCommand(
                "settings put global airplane_mode " + state
                        + "; am broadcast -a android.intent.action.AIRPLANE_MODE --ez state " + bool
        );
    }

    private static void setBluetoothEnabled(boolean enabled) {
        executeRootCommand("svc bluetooth " + (enabled ? "enable" : "disable"));
    }

    private static void setWifiEnabled(boolean enabled) {
        executeRootCommand("svc wifi " + (enabled ? "enable" : "disable"));
    }

    private static void phoneCall(String number, int state) {
        if (state == 0) {
            executeRootCommand(
                    "am start -a android.intent.action.CALL -d "
                            + shellQuote("tel:" + Uri.encode(number))
            );
        } else {
            executeRootCommand("input keyevent 6");
        }
    }

    private static void sendSms(Context context, String number, String content) {
        if (context.checkSelfPermission(Manifest.permission.SEND_SMS)
                != PackageManager.PERMISSION_GRANTED) {
            throw new IllegalArgumentException("未授予发送短信权限");
        }
        SmsManager.getDefault().sendTextMessage(number, null, content, null, null);
    }

    private static void vibrate(Context context, int durationMs) {
        if (durationMs == 0) {
            return;
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            VibratorManager vibratorManager = (VibratorManager) context.getSystemService(
                    Context.VIBRATOR_MANAGER_SERVICE
            );
            Vibrator vibrator = vibratorManager == null ? null : vibratorManager.getDefaultVibrator();
            if (vibrator != null && vibrator.hasVibrator()) {
                vibrator.vibrate(VibrationEffect.createOneShot(durationMs, VibrationEffect.DEFAULT_AMPLITUDE));
            }
            return;
        }

        Vibrator vibrator = (Vibrator) context.getSystemService(Context.VIBRATOR_SERVICE);
        if (vibrator != null && vibrator.hasVibrator()) {
            vibrator.vibrate(durationMs);
        }
    }

    private static int batteryLevel(Context context) {
        Intent battery = context.registerReceiver(
                (BroadcastReceiver) null,
                new IntentFilter(Intent.ACTION_BATTERY_CHANGED)
        );
        if (battery == null) {
            return -1;
        }
        int level = battery.getIntExtra(BatteryManager.EXTRA_LEVEL, -1);
        int scale = battery.getIntExtra(BatteryManager.EXTRA_SCALE, -1);
        return level < 0 || scale <= 0 ? -1 : Math.round(level * 100f / scale);
    }

    private static String cpuAbi(int index) {
        String[] supportedAbis = Build.SUPPORTED_ABIS;
        return supportedAbis != null && index >= 0 && index < supportedAbis.length
                ? supportedAbis[index]
                : "";
    }

    private static int cpuArch() {
        String abi = cpuAbi(0).toLowerCase(Locale.ROOT);
        return abi.contains("x86") ? 0 : 1;
    }

    private static DisplayMetrics displayMetrics(Context context) {
        WindowManager windowManager = (WindowManager) context.getSystemService(Context.WINDOW_SERVICE);
        if (windowManager == null) {
            throw new IllegalArgumentException("窗口服务不可用");
        }
        Display display = windowManager.getDefaultDisplay();
        DisplayMetrics metrics = new DisplayMetrics();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.JELLY_BEAN_MR1) {
            display.getRealMetrics(metrics);
        } else {
            display.getMetrics(metrics);
        }
        return metrics;
    }

    private static int displayRotation(Context context) {
        WindowManager windowManager = (WindowManager) context.getSystemService(Context.WINDOW_SERVICE);
        if (windowManager == null) {
            throw new IllegalArgumentException("窗口服务不可用");
        }
        return windowManager.getDefaultDisplay().getRotation();
    }

    private static JSONObject displaySize(Context context) throws JSONException {
        DisplayMetrics metrics = displayMetrics(context);
        JSONObject result = new JSONObject();
        result.put("width", metrics.widthPixels);
        result.put("height", metrics.heightPixels);
        return result;
    }

    private static JSONObject displayInfo(Context context) throws JSONException {
        DisplayMetrics metrics = displayMetrics(context);
        JSONObject result = displaySize(context);
        result.put("dpi", metrics.densityDpi);
        result.put("density", metrics.density);
        result.put("scaledDensity", metrics.scaledDensity);
        result.put("xdpi", metrics.xdpi);
        result.put("ydpi", metrics.ydpi);
        result.put("rotate", displayRotation(context));
        return result;
    }

    private static JSONArray sensorsInfo(Context context) throws JSONException {
        android.hardware.SensorManager manager = (android.hardware.SensorManager) context.getSystemService(
                Context.SENSOR_SERVICE
        );
        JSONArray result = new JSONArray();
        if (manager == null) {
            return result;
        }
        for (android.hardware.Sensor sensor : manager.getSensorList(android.hardware.Sensor.TYPE_ALL)) {
            JSONObject item = new JSONObject();
            item.put("name", sensor.getName());
            item.put("vendor", sensor.getVendor());
            item.put("type", sensor.getType());
            item.put("typeName", sensor.getStringType());
            item.put("version", sensor.getVersion());
            item.put("maximumRange", sensor.getMaximumRange());
            item.put("resolution", sensor.getResolution());
            item.put("power", sensor.getPower());
            item.put("minDelay", sensor.getMinDelay());
            result.put(item);
        }
        return result;
    }

    @SuppressWarnings("deprecation")
    private static String simSerialNumber(Context context) {
        if (context.checkSelfPermission(Manifest.permission.READ_PHONE_STATE)
                != PackageManager.PERMISSION_GRANTED) {
            return null;
        }
        TelephonyManager manager = (TelephonyManager) context.getSystemService(Context.TELEPHONY_SERVICE);
        return manager == null ? null : manager.getSimSerialNumber();
    }

    private static String subscriberId(Context context) {
        if (context.checkSelfPermission(Manifest.permission.READ_PHONE_STATE)
                != PackageManager.PERMISSION_GRANTED) {
            return null;
        }
        TelephonyManager manager = (TelephonyManager) context.getSystemService(Context.TELEPHONY_SERVICE);
        return manager == null ? null : manager.getSubscriberId();
    }

    private static String wifiMacAddress() {
        try {
            Enumeration<NetworkInterface> interfaces = NetworkInterface.getNetworkInterfaces();
            if (interfaces == null) {
                return null;
            }
            while (interfaces.hasMoreElements()) {
                NetworkInterface networkInterface = interfaces.nextElement();
                String name = networkInterface.getName();
                if (name == null || (!name.startsWith("wlan") && !name.startsWith("wifi"))) {
                    continue;
                }
                byte[] address = networkInterface.getHardwareAddress();
                if (address == null || address.length == 0) {
                    continue;
                }
                StringBuilder output = new StringBuilder(address.length * 3 - 1);
                for (int index = 0; index < address.length; index++) {
                    if (index > 0) {
                        output.append(':');
                    }
                    output.append(String.format(Locale.ROOT, "%02X", address[index] & 0xff));
                }
                return output.toString();
            }
        } catch (Exception ignored) {
            // 设备不暴露网卡 MAC 时按 API 契约返回 nil，不伪造地址。
        }
        return null;
    }

    private static int runEnvironmentType(Context context) {
        if (EngineSettings.isRootModeEnabled(context) && RootDaemonClient.isReady(context)) {
            return 0;
        }
        return AutomationAccessibilityService.isEnabled() ? 1 : -1;
    }

    private static String requireString(JSONObject arguments, String name) {
        String value = nullableString(arguments, name);
        if (value == null || value.isEmpty()) {
            throw new IllegalArgumentException(name + " 参数不能为空");
        }
        return value;
    }

    private static String nullableString(JSONObject arguments, String name) {
        Object value = arguments.opt(name);
        if (value == null || value == JSONObject.NULL) {
            return null;
        }
        if (!(value instanceof String)) {
            throw new IllegalArgumentException(name + " 参数必须是字符串");
        }
        return (String) value;
    }

    private static int requireNonNegativeInt(JSONObject arguments, String name) {
        if (!arguments.has(name)) {
            throw new IllegalArgumentException(name + " 参数不能为空");
        }
        int value = arguments.optInt(name, -1);
        if (value < 0) {
            throw new IllegalArgumentException(name + " 参数必须是大于等于 0 的整数");
        }
        return value;
    }

    /** 把普通文本安全地作为一个 shell 参数传给 RootDaemon。 */
    private static String shellQuote(String value) {
        return "'" + value.replace("'", "'\\\"'\\\"'") + "'";
    }

    private static String success(Object value) {
        try {
            JSONObject result = new JSONObject();
            result.put("ok", true);
            result.put("value", value == null ? JSONObject.NULL : value);
            return result.toString();
        } catch (JSONException exception) {
            return failure("设备结果编码失败");
        }
    }

    private static String failure(String error) {
        try {
            JSONObject result = new JSONObject();
            result.put("ok", false);
            result.put("error", error == null || error.isEmpty() ? "设备操作失败" : error);
            return result.toString();
        } catch (JSONException exception) {
            return "{\"ok\":false,\"error\":\"设备操作失败\"}";
        }
    }

    private static final class ComponentName {
        private final String packageName;
        private final String activityName;

        private ComponentName(String packageName, String activityName) {
            this.packageName = packageName;
            this.activityName = activityName;
        }
    }
}
