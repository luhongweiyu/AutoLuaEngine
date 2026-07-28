/**
 * 文件用途：提供与懒人精灵同包名、同类名和同参数顺序的 PaddleOcr Java 兼容入口。
 */
package com.nx.assist.lua;

import android.graphics.Bitmap;

import com.xiaoyv.engine.OcrPlatformBridge;

/**
 * PP-OCRv4 ONNX 兼容类。
 *
 * 当前小鱼精灵使用 ONNX Runtime；NCNN 参数/bin 模型没有可执行后端，因此对应加载函数
 * 明确返回 false，不会把另一种模型格式伪装为成功。
 */
public final class PaddleOcr {
    private PaddleOcr() {
    }

    /** true 加载内置 ONNX 模型；false 请求 NCNN 时返回 false。 */
    public static boolean loadModel(boolean isUseOnnxModel) {
        return OcrPlatformBridge.loadPaddleCompatBuiltin(isUseOnnxModel);
    }

    /** 加载自定义 PP-OCR ONNX 检测、方向分类、识别和字典文件。 */
    public static boolean loadOnnxModel(
            String modelDetPath,
            String modelClsPath,
            String modelRecPath,
            String keyTxt
    ) {
        return OcrPlatformBridge.loadPaddleCompatOnnx(
                modelDetPath,
                modelClsPath,
                modelRecPath,
                keyTxt
        );
    }

    /** 当前没有 NCNN 运行时，真实返回不支持。 */
    public static boolean loadNnccModel(
            String detParams,
            String recParams,
            String detBin,
            String recBin,
            String keyTxt
    ) {
        return false;
    }

    /** 识别 Bitmap，成功返回 JSON 数组字符串，失败返回 null。 */
    public static String detect(Bitmap bitmap) {
        return OcrPlatformBridge.detectPaddleCompat(bitmap, 0, 255, 255, 255);
    }

    /** 给 Bitmap 添加指定颜色边框后识别，结果坐标仍相对原 Bitmap。 */
    public static String detectWithPadding(
            Bitmap bitmap,
            int padding,
            int red,
            int green,
            int blue
    ) {
        return OcrPlatformBridge.detectPaddleCompat(bitmap, padding, red, green, blue);
    }
}
