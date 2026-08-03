/**
 * 文件用途：管理可选 libxiaoyv_yolo.so，并向 libengine.so 提供固定的 YOLO 平台调用入口。
 */
package com.xiaoyv.engine;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.IOException;
import java.nio.ByteBuffer;

/**
 * YOLO 可选运行时平台桥。
 *
 * App 基础包不包含该 SO；用户在扩展页导入 yolo/libxiaoyv_yolo.so 后，Worker 中的 C ABI
 * 或插件实际加载、检测模型时才按需加载它。文件名、CPU ABI、依赖和内容均由用户自己负责；
 * 导入或查询状态不执行 native 代码。模型、标签和图片都只接收普通可读文件或由 native 临时
 * 交付的紧凑 RGBA 直接缓冲，不读取 ALPKG。
 */
public final class YoloPlatformBridge {
    private static final String RUNTIME_FILE_NAME = "yolo/libxiaoyv_yolo.so";
    private static final Object LOAD_LOCK = new Object();
    private static boolean nativeLoaded;
    private static String nativeLoadError = "";
    private static String nativeLoadedLibraryPath = "";

    private YoloPlatformBridge() {
    }

    /** libengine.so 的固定模型管理/运行时入口。 */
    public static String call(String operation, String argumentsJson) {
        try {
            JSONObject arguments = new JSONObject(argumentsJson == null ? "{}" : argumentsJson);
            if ("runtimeInfo".equals(operation)) {
                return runtimeInfo();
            }
            if ("load".equals(operation)) {
                if (!ensureNativeLoaded()) {
                    return missingRuntime();
                }
                return load(arguments);
            }
            if ("release".equals(operation)) {
                if (!nativeLoaded) {
                    return success(objectOf("released", false));
                }
                return nativeRelease(required(arguments, "name"));
            }
            if ("isLoaded".equals(operation)) {
                if (!nativeLoaded) {
                    return success(objectOf("loaded", false));
                }
                return nativeIsLoaded(required(arguments, "name"));
            }
            return failure("未知 YOLO 操作：" + safe(operation));
        } catch (JSONException exception) {
            return failure("YOLO 参数 JSON 无效：" + safeMessage(exception));
        } catch (RuntimeException | LinkageError exception) {
            return failure("YOLO 平台调用失败：" + safeMessage(exception));
        }
    }

    /**
     * 由 libengine.so 把截图或普通图片解码为直接 RGBA 缓冲后调用。
     *
     * operation 目前固定为 detect，保留该参数是为了和其他平台桥的固定命令风格一致，不能借此
     * 反射任意 native 方法。
     */
    public static String detectRgba(
            String operation,
            String argumentsJson,
            ByteBuffer pixels,
            int width,
            int height
    ) {
        try {
            if (!"detect".equals(operation)) {
                return failure("未知 YOLO RGBA 操作：" + safe(operation));
            }
            if (!ensureNativeLoaded()) {
                return missingRuntime();
            }
            if (pixels == null || !pixels.isDirect()) {
                return failure("YOLO 检测需要 native 提供的 RGBA 直接缓冲区");
            }

            JSONObject arguments = new JSONObject(argumentsJson == null ? "{}" : argumentsJson);
            String name = required(arguments, "name");
            if (name.isEmpty()) {
                return failure("YOLO 模型名称不能为空");
            }
            JSONObject options = options(arguments);
            int targetSize = options.optInt("targetSize", 640);
            int threads = options.optInt("threads", 2);
            float probabilityThreshold = (float) options.optDouble("probThreshold", 0.25D);
            float nmsThreshold = (float) options.optDouble("nmsThreshold", 0.45D);
            if (targetSize < 32 || targetSize > 4096 || threads < 1 || threads > 32
                    || !finiteInRange(probabilityThreshold, 0.0F, 1.0F)
                    || !finiteInRange(nmsThreshold, 0.0F, 1.0F)) {
                return failure("YOLO 检测 options 超出支持范围");
            }

            return nativeDetectRgba(
                    name,
                    pixels,
                    width,
                    height,
                    arguments.optInt("left", 0),
                    arguments.optInt("top", 0),
                    arguments.optInt("right", 0),
                    arguments.optInt("bottom", 0),
                    targetSize,
                    threads,
                    probabilityThreshold,
                    nmsThreshold
            );
        } catch (JSONException exception) {
            return failure("YOLO 检测参数 JSON 无效：" + safeMessage(exception));
        } catch (RuntimeException | LinkageError exception) {
            return failure("YOLO 检测失败：" + safeMessage(exception));
        }
    }

    /**
     * 仅查询导入状态，不触发 System.load。
     *
     * available 表示同名文件已经导入、可在脚本执行时尝试加载；不代表用户提供的文件一定适合
     * 当前 CPU 或接口。loaded 才表示本引擎进程已经成功加载。
     */
    private static String runtimeInfo() {
        boolean imported = ExtensionCatalog.getImportedExtension(
                AndroidHostBridge.applicationContext(),
                RUNTIME_FILE_NAME
        ) != null;
        if (!nativeLoaded) {
            return success(objectOf(
                    "available", imported,
                    "loaded", false,
                    "fileName", RUNTIME_FILE_NAME,
                    "directory", ExtensionCatalog.getExtensionDirectoryDisplayPath(),
                    "error", nativeLoadError
            ));
        }
        try {
            JSONObject envelope = new JSONObject(nativeRuntimeInfo());
            JSONObject data = envelope.optJSONObject("data");
            if (envelope.optBoolean("ok", false) && data != null) {
                data.put("available", true);
                data.put("loaded", true);
                data.put("fileName", RUNTIME_FILE_NAME);
                data.put("loadedLibrary", nativeLoadedLibraryPath);
                return envelope.toString();
            }
        } catch (JSONException ignored) {
            // 独立 SO 的 JNI 返回异常时保留其原始内容，core/api 会统一报告协议错误。
        }
        return nativeRuntimeInfo();
    }

    /** 规范化模型文件，并把可变 blob 配置交给独立 SO。 */
    private static String load(JSONObject arguments) throws JSONException {
        String name = required(arguments, "name");
        String labelsPath = canonicalFile(required(arguments, "labels"));
        String paramPath = canonicalFile(required(arguments, "param"));
        String binPath = canonicalFile(required(arguments, "bin"));
        if (name.isEmpty() || labelsPath.isEmpty() || paramPath.isEmpty() || binPath.isEmpty()) {
            return failure("YOLO 模型名称、labels、param 和 bin 必须是可读的普通文件");
        }

        JSONObject options = options(arguments);
        String input = options.optString("input", "images").trim();
        JSONArray outputs = options.optJSONArray("outputs");
        String output8 = "output";
        String output16 = "353";
        String output32 = "367";
        if (outputs != null) {
            if (outputs.length() != 3) {
                return failure("YOLO load options.outputs 必须恰好包含三个 blob 名称");
            }
            output8 = outputs.optString(0, "").trim();
            output16 = outputs.optString(1, "").trim();
            output32 = outputs.optString(2, "").trim();
        }
        if (input.isEmpty() || output8.isEmpty() || output16.isEmpty() || output32.isEmpty()) {
            return failure("YOLO 输入和输出 blob 名称不能为空");
        }
        return nativeLoad(
                name,
                labelsPath,
                paramPath,
                binPath,
                input,
                output8,
                output16,
                output32,
                options.optBoolean("useGpu", false)
        );
    }

    /**
     * 首次实际使用 YOLO 时才加载已导入文件；基础 APK 不含该 SO 也能正常启动。
     *
     * 没有成功加载前每次调用都会重新读取导入副本；用户重新导入替换文件后无需重装 APK。已经
     * 成功加载的 native 库不能在同一进程中卸载或热替换，替换后重启引擎进程即可使用新文件。
     */
    private static boolean ensureNativeLoaded() {
        synchronized (LOAD_LOCK) {
            if (nativeLoaded) {
                return true;
            }

            OptionalNativeRuntimeLoader.LoadResult result = OptionalNativeRuntimeLoader.loadImported(
                    AndroidHostBridge.applicationContext(),
                    RUNTIME_FILE_NAME
            );
            if (!result.loaded) {
                nativeLoadError = result.error;
                return false;
            }
            nativeLoaded = true;
            nativeLoadError = "";
            nativeLoadedLibraryPath = result.loadedLibraryPath;
            return nativeLoaded;
        }
    }

    private static String missingRuntime() {
        return failure(nativeLoadError.isEmpty()
                ? "未导入 YOLO 运行时 " + RUNTIME_FILE_NAME + "；请先将文件复制到 "
                        + ExtensionCatalog.getExtensionDirectoryDisplayPath()
                        + "，再在扩展页点击导入"
                : nativeLoadError);
    }

    /** options 可省略；显式给出非对象值时拒绝，以免默默忽略拼写错误。 */
    private static JSONObject options(JSONObject arguments) throws JSONException {
        if (!arguments.has("options") || arguments.isNull("options")) {
            return new JSONObject();
        }
        JSONObject value = arguments.optJSONObject("options");
        if (value == null) {
            throw new JSONException("options 必须是对象");
        }
        return value;
    }

    /** 只允许普通文件路径；file:// 是普通文件路径的等价写法。 */
    private static String canonicalFile(String path) {
        try {
            String normalized = safe(path).startsWith("file://")
                    ? safe(path).substring("file://".length())
                    : safe(path);
            File file = new File(normalized).getCanonicalFile();
            return file.isFile() && file.canRead() ? file.getPath() : "";
        } catch (IOException exception) {
            return "";
        }
    }

    private static boolean finiteInRange(float value, float minimum, float maximum) {
        return !Float.isNaN(value) && !Float.isInfinite(value) && value >= minimum && value <= maximum;
    }

    private static String required(JSONObject object, String key) {
        return object.optString(key, "").trim();
    }

    private static String success(JSONObject data) {
        try {
            JSONObject root = new JSONObject();
            root.put("ok", true);
            root.put("data", data == null ? new JSONObject() : data);
            return root.toString();
        } catch (JSONException exception) {
            return "{\"ok\":false,\"error\":\"YOLO 返回结果编码失败\"}";
        }
    }

    private static String failure(String message) {
        try {
            JSONObject root = new JSONObject();
            root.put("ok", false);
            root.put("error", safe(message));
            return root.toString();
        } catch (JSONException exception) {
            return "{\"ok\":false,\"error\":\"YOLO 平台调用失败\"}";
        }
    }

    private static JSONObject objectOf(Object... values) {
        JSONObject result = new JSONObject();
        try {
            for (int index = 0; index + 1 < values.length; index += 2) {
                result.put(String.valueOf(values[index]), values[index + 1]);
            }
            return result;
        } catch (JSONException exception) {
            throw new IllegalStateException("YOLO 返回结果编码失败", exception);
        }
    }

    private static String safe(String value) {
        return value == null ? "" : value;
    }

    private static String safeMessage(Throwable throwable) {
        String message = throwable.getMessage();
        return message == null || message.isEmpty() ? throwable.getClass().getSimpleName() : message;
    }

    private static native String nativeLoad(
            String name,
            String labelsPath,
            String paramPath,
            String binPath,
            String inputBlob,
            String output8Blob,
            String output16Blob,
            String output32Blob,
            boolean useGpu
    );

    private static native String nativeRelease(String name);

    private static native String nativeIsLoaded(String name);

    private static native String nativeDetectRgba(
            String name,
            ByteBuffer pixels,
            int width,
            int height,
            int left,
            int top,
            int right,
            int bottom,
            int targetSize,
            int threads,
            float probabilityThreshold,
            float nmsThreshold
    );

    private static native String nativeRuntimeInfo();
}
