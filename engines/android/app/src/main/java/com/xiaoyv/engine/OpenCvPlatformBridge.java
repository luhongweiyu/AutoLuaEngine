/**
 * 文件用途：用官方 OpenCV Android AAR 实现少量无法由现有点阵核心等价替代的图像能力。
 */
package com.xiaoyv.engine;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;
import org.opencv.core.CvType;
import org.opencv.core.Mat;
import org.opencv.core.Rect;
import org.opencv.imgproc.Imgproc;

import java.util.Arrays;

/**
 * OpenCV 平台适配层。
 *
 * 当前只暴露霍夫找圆。模板匹配、找色和 OCR 继续走已有 native 热路径，避免把两个图像
 * 实现混在一起。输入图像来自当前引擎截图缓存，因此 keepCapture 和自定义 Bitmap 缓存
 * 对 findCircle 同样生效。
 */
public final class OpenCvPlatformBridge {
    private static final String CPP_RUNTIME_FILE_NAME = "opencv/libc++_shared.so";
    private static final String OPENCV_RUNTIME_FILE_NAME = "opencv/libopencv_java4.so";
    private static final Object INITIALIZE_LOCK = new Object();
    private static volatile boolean initialized;

    private OpenCvPlatformBridge() {
    }

    /**
     * 供已有 Java 互操作入口和 LuaEngine 兼容层在真正使用 OpenCV 前调用。
     *
     * 这不是新的脚本 API；它保证现有 `import("org.opencv.*")` 与 `cv.snapShot` 在
     * OpenCV native 库已从基础 APK 拆出后仍保持原来的可用方式。
     */
    public static void ensureRuntimeLoaded() {
        ensureInitialized();
    }

    static Object call(String operation, JSONObject arguments) throws JSONException {
        if (!"image.findCircle".equals(operation)) {
            throw new IllegalArgumentException("不支持的 OpenCV 能力：" + operation);
        }
        ensureInitialized();
        return findCircles(arguments);
    }

    private static JSONArray findCircles(JSONObject arguments) throws JSONException {
        double dp = requirePositiveNumber(arguments, "dp");
        double minDistance = requirePositiveNumber(arguments, "minDist");
        double edgeThreshold = requirePositiveNumber(arguments, "param1");
        double centerThreshold = requirePositiveNumber(arguments, "param2");
        int minimumRadius = requireNonNegativeInt(arguments, "minRadius");
        int maximumRadius = requireNonNegativeInt(arguments, "maxRadius");
        if (maximumRadius != 0 && maximumRadius < minimumRadius) {
            throw new IllegalArgumentException("maxRadius 不能小于 minRadius");
        }

        Frame frame = readCurrentFrame();
        Region region = readRegion(arguments, frame.width, frame.height);
        Mat screen = new Mat(frame.height, frame.width, CvType.CV_8UC4);
        Mat area = null;
        Mat gray = new Mat();
        Mat blurred = new Mat();
        Mat circles = new Mat();
        try {
            screen.put(0, 0, frame.rgba);
            area = screen.submat(new Rect(
                    region.left,
                    region.top,
                    region.right - region.left + 1,
                    region.bottom - region.top + 1
            ));
            Imgproc.cvtColor(area, gray, Imgproc.COLOR_RGBA2GRAY);
            Imgproc.medianBlur(gray, blurred, 3);
            Imgproc.HoughCircles(
                    blurred,
                    circles,
                    Imgproc.HOUGH_GRADIENT,
                    dp,
                    minDistance,
                    edgeThreshold,
                    centerThreshold,
                    minimumRadius,
                    maximumRadius
            );

            JSONArray result = new JSONArray();
            for (int index = 0; index < circles.cols(); index++) {
                double[] value = circles.get(0, index);
                if (value == null || value.length < 3) {
                    continue;
                }
                JSONObject circle = new JSONObject();
                circle.put("x", region.left + (int) Math.round(value[0]));
                circle.put("y", region.top + (int) Math.round(value[1]));
                circle.put("r", Math.max(0, (int) Math.round(value[2])));
                result.put(circle);
            }
            return result;
        } finally {
            circles.release();
            blurred.release();
            gray.release();
            if (area != null) {
                area.release();
            }
            screen.release();
        }
    }

    private static void ensureInitialized() {
        if (initialized) {
            return;
        }
        synchronized (INITIALIZE_LOCK) {
            if (initialized) {
                return;
            }
            OptionalNativeRuntimeLoader.LoadResult result = OptionalNativeRuntimeLoader.loadImported(
                    AndroidHostBridge.applicationContext(),
                    CPP_RUNTIME_FILE_NAME,
                    OPENCV_RUNTIME_FILE_NAME
            );
            if (!result.loaded) {
                throw new IllegalStateException(result.error);
            }
            // OpenCVLoader.initLocal() 会再次用 System.loadLibrary 从 APK 的 native 目录查找
            // libopencv_java4.so。可选运行时已由上面的绝对路径按需加载，直接使用其 Java API
            // 即可；再次调用反而会把 APK 内不存在该库误报为初始化失败。
            initialized = true;
        }
    }

    private static Frame readCurrentFrame() {
        byte[] encoded = NativeEngine.getScreenFrame();
        if (encoded == null || encoded.length < 12
                || encoded[0] != 'X' || encoded[1] != 'Y'
                || encoded[2] != 'V' || encoded[3] != 'F') {
            throw new IllegalStateException("当前截图缓存无效");
        }

        int width = readLittleEndianInt(encoded, 4);
        int height = readLittleEndianInt(encoded, 8);
        long expectedLength = 12L + (long) width * height * 4L;
        if (width <= 0 || height <= 0 || expectedLength != encoded.length) {
            throw new IllegalStateException("当前截图尺寸或 RGBA 点阵无效");
        }
        return new Frame(width, height, Arrays.copyOfRange(encoded, 12, encoded.length));
    }

    private static Region readRegion(JSONObject arguments, int width, int height) {
        int x1 = arguments.optInt("x1", 0);
        int y1 = arguments.optInt("y1", 0);
        int x2 = arguments.optInt("x2", 0);
        int y2 = arguments.optInt("y2", 0);
        if (x1 == 0 && y1 == 0 && x2 == 0 && y2 == 0) {
            return new Region(0, 0, width - 1, height - 1);
        }

        int left = Math.min(x1, x2);
        int top = Math.min(y1, y2);
        int right = Math.max(x1, x2);
        int bottom = Math.max(y1, y2);
        if (left < 0 || top < 0 || right >= width || bottom >= height) {
            throw new IllegalArgumentException("找圆区域超出当前截图范围");
        }
        return new Region(left, top, right, bottom);
    }

    private static double requirePositiveNumber(JSONObject arguments, String name) {
        if (!arguments.has(name)) {
            throw new IllegalArgumentException(name + " 参数不能为空");
        }
        double value = arguments.optDouble(name, Double.NaN);
        if (!Double.isFinite(value) || value <= 0) {
            throw new IllegalArgumentException(name + " 必须是大于 0 的有限数值");
        }
        return value;
    }

    private static int requireNonNegativeInt(JSONObject arguments, String name) {
        if (!arguments.has(name)) {
            throw new IllegalArgumentException(name + " 参数不能为空");
        }
        Object value = arguments.opt(name);
        if (!(value instanceof Number)) {
            throw new IllegalArgumentException(name + " 必须是非负整数");
        }
        double numeric = ((Number) value).doubleValue();
        int integer = ((Number) value).intValue();
        if (!Double.isFinite(numeric) || numeric != integer || integer < 0) {
            throw new IllegalArgumentException(name + " 必须是非负整数");
        }
        return integer;
    }

    private static int readLittleEndianInt(byte[] value, int offset) {
        return (value[offset] & 0xff)
                | ((value[offset + 1] & 0xff) << 8)
                | ((value[offset + 2] & 0xff) << 16)
                | ((value[offset + 3] & 0xff) << 24);
    }

    private static final class Frame {
        private final int width;
        private final int height;
        private final byte[] rgba;

        private Frame(int width, int height, byte[] rgba) {
            this.width = width;
            this.height = height;
            this.rgba = rgba;
        }
    }

    private static final class Region {
        private final int left;
        private final int top;
        private final int right;
        private final int bottom;

        private Region(int left, int top, int right, int bottom) {
            this.left = left;
            this.top = top;
            this.right = right;
            this.bottom = bottom;
        }
    }
}
