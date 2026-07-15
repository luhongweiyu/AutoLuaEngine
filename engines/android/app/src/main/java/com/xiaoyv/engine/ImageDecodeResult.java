/**
 * 文件用途：承载 Java 图片解码结果，供 libengine.so 通过 AndroidHostBridge 读取 RGBA 点阵。
 */
package com.xiaoyv.engine;

/**
 * 图片解码结果。
 *
 * 字段保持公开且只承载基础类型，JNI 可以直接读取，不需要为模板匹配维护 Java Bitmap
 * 生命周期。rgba 始终是紧凑 RGBA8888，长度为 width * height * 4。
 */
public final class ImageDecodeResult {
    public final boolean success;
    public final int width;
    public final int height;
    public final byte[] rgba;
    public final long sourceStamp;
    public final String error;

    private ImageDecodeResult(
            boolean success,
            int width,
            int height,
            byte[] rgba,
            long sourceStamp,
            String error
    ) {
        this.success = success;
        this.width = width;
        this.height = height;
        this.rgba = rgba;
        this.sourceStamp = sourceStamp;
        this.error = error == null ? "" : error;
    }

    /** 创建成功结果。 */
    public static ImageDecodeResult success(int width, int height, byte[] rgba, long sourceStamp) {
        return new ImageDecodeResult(true, width, height, rgba, sourceStamp, "");
    }

    /** 创建失败结果，避免 JNI 侧通过异常文本猜测图片状态。 */
    public static ImageDecodeResult failure(String error) {
        return new ImageDecodeResult(false, 0, 0, null, 0L, error);
    }
}
