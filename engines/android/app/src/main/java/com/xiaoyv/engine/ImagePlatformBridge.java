/**
 * 文件用途：实现 Android 图片文件解码与 RGBA 保存，供 libengine.so 的找图和截图保存能力复用。
 */
package com.xiaoyv.engine;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;

/**
 * 图片平台桥。
 *
 * 图片格式解码和编码由 Android Framework 完成；模板匹配、截图缓存和脚本 API 逻辑仍留在
 * libengine.so。解码后的图片立即转换为紧凑 RGBA 点阵，不把 Bitmap 交给 native 长期持有。
 */
public final class ImagePlatformBridge {
    private ImagePlatformBridge() {
    }

    /**
     * 解码普通图片文件。
     *
     * 支持 Android BitmapFactory 能识别的 PNG、JPEG、WebP 等格式。sourceStamp 只用于 native
     * 模板缓存判断文件是否被修改，读取失败时返回结构化失败结果。
     */
    public static ImageDecodeResult decodeFile(String path) {
        if (path == null || path.trim().isEmpty()) {
            return ImageDecodeResult.failure("图片路径不能为空");
        }

        String normalizedPath = path.startsWith("file://") ? path.substring("file://".length()) : path;
        File file = new File(normalizedPath);
        if (!file.isFile()) {
            return ImageDecodeResult.failure("图片文件不存在：" + path);
        }

        Bitmap bitmap = null;
        try {
            bitmap = BitmapFactory.decodeFile(file.getAbsolutePath());
            if (bitmap == null) {
                return ImageDecodeResult.failure("无法解码图片：" + path);
            }
            return bitmapToRgba(bitmap, file.lastModified());
        } catch (RuntimeException exception) {
            return ImageDecodeResult.failure("解码图片失败：" + safeMessage(exception));
        } finally {
            recycle(bitmap);
        }
    }

    /**
     * 解码 native 传入的图片原始字节。
     *
     * 该入口用于 ALPKG 内的图片资源。包资源不落地到临时目录，native 读取 ZIP 数据后直接
     * 通过 DirectByteBuffer 传入，避免额外磁盘 IO。
     */
    public static ImageDecodeResult decodeBytes(ByteBuffer source, int size) {
        if (source == null || size <= 0 || size > source.capacity()) {
            return ImageDecodeResult.failure("图片数据无效");
        }

        byte[] bytes = new byte[size];
        try {
            ByteBuffer view = source.duplicate();
            view.position(0);
            view.get(bytes, 0, size);
        } catch (RuntimeException exception) {
            return ImageDecodeResult.failure("读取图片数据失败：" + safeMessage(exception));
        }

        Bitmap bitmap = null;
        try {
            bitmap = BitmapFactory.decodeByteArray(bytes, 0, bytes.length);
            if (bitmap == null) {
                return ImageDecodeResult.failure("无法解码包内图片资源");
            }
            return bitmapToRgba(bitmap, 0L);
        } catch (RuntimeException exception) {
            return ImageDecodeResult.failure("解码包内图片失败：" + safeMessage(exception));
        } finally {
            recycle(bitmap);
        }
    }

    /**
     * 将 native 紧凑 RGBA8888 点阵保存为图片。
     *
     * 截图缓存本身不经过图片编码；只有脚本显式调用 capture(path) 时才走本方法。PNG 为默认
     * 格式，路径以 .jpg/.jpeg/.webp 结尾时使用对应压缩器。
     */
    public static boolean saveRgba(
            ByteBuffer source,
            int width,
            int height,
            int size,
            int left,
            int top,
            int right,
            int bottom,
            String path
    ) {
        if (source == null || width <= 0 || height <= 0 || path == null || path.trim().isEmpty()) {
            return false;
        }

        long expected = (long) width * (long) height * 4L;
        if (expected <= 0L || expected > Integer.MAX_VALUE || size != (int) expected || size > source.capacity()) {
            return false;
        }
        if (left < 0 || top < 0 || right > width || bottom > height || left >= right || top >= bottom) {
            return false;
        }

        int outputWidth = right - left;
        int outputHeight = bottom - top;
        long outputPixels = (long) outputWidth * (long) outputHeight;
        if (outputPixels <= 0L || outputPixels > Integer.MAX_VALUE) {
            return false;
        }

        int[] colors = new int[(int) outputPixels];
        try {
            ByteBuffer view = source.duplicate();
            int targetIndex = 0;
            for (int y = top; y < bottom; y++) {
                view.position((y * width + left) * 4);
                for (int x = left; x < right; x++) {
                    int red = view.get() & 0xff;
                    int green = view.get() & 0xff;
                    int blue = view.get() & 0xff;
                    int alpha = view.get() & 0xff;
                    colors[targetIndex++] = (alpha << 24) | (red << 16) | (green << 8) | blue;
                }
            }
        } catch (RuntimeException exception) {
            return false;
        }

        String normalizedPath = path.startsWith("file://") ? path.substring("file://".length()) : path;
        File file = new File(normalizedPath);
        File parent = file.getParentFile();
        if (parent != null && !parent.exists() && !parent.mkdirs()) {
            return false;
        }

        Bitmap bitmap = null;
        try {
            bitmap = Bitmap.createBitmap(colors, outputWidth, outputHeight, Bitmap.Config.ARGB_8888);
            Bitmap.CompressFormat format = outputFormat(file.getName());
            int quality = format == Bitmap.CompressFormat.PNG ? 100 : 95;
            try (FileOutputStream output = new FileOutputStream(file, false)) {
                return bitmap.compress(format, quality, output);
            }
        } catch (IOException | RuntimeException exception) {
            return false;
        } finally {
            recycle(bitmap);
        }
    }

    /** 将 Bitmap 转为与 libengine.so 截图缓存一致的 RGBA8888。 */
    private static ImageDecodeResult bitmapToRgba(Bitmap source, long sourceStamp) {
        if (source.getWidth() <= 0 || source.getHeight() <= 0) {
            return ImageDecodeResult.failure("图片尺寸无效");
        }

        long byteCount = (long) source.getWidth() * (long) source.getHeight() * 4L;
        if (byteCount > Integer.MAX_VALUE) {
            return ImageDecodeResult.failure("图片过大");
        }

        int width = source.getWidth();
        int height = source.getHeight();
        int[] colors = new int[width * height];
        source.getPixels(colors, 0, width, 0, 0, width, height);
        byte[] rgba = new byte[(int) byteCount];
        int target = 0;
        for (int color : colors) {
            rgba[target++] = (byte) ((color >> 16) & 0xff);
            rgba[target++] = (byte) ((color >> 8) & 0xff);
            rgba[target++] = (byte) (color & 0xff);
            rgba[target++] = (byte) ((color >>> 24) & 0xff);
        }
        return ImageDecodeResult.success(width, height, rgba, sourceStamp);
    }

    /** 根据文件扩展名选择输出格式。 */
    private static Bitmap.CompressFormat outputFormat(String name) {
        String lowercase = name == null ? "" : name.toLowerCase();
        if (lowercase.endsWith(".jpg") || lowercase.endsWith(".jpeg")) {
            return Bitmap.CompressFormat.JPEG;
        }
        if (lowercase.endsWith(".webp")) {
            return Bitmap.CompressFormat.WEBP_LOSSLESS;
        }
        return Bitmap.CompressFormat.PNG;
    }

    /** 统一释放临时 Bitmap，避免图片解码频繁调用时堆积 native 像素缓冲。 */
    private static void recycle(Bitmap bitmap) {
        if (bitmap != null && !bitmap.isRecycled()) {
            bitmap.recycle();
        }
    }

    /** RuntimeException 可能没有消息，避免把 null 直接拼接进脚本错误。 */
    private static String safeMessage(RuntimeException exception) {
        String message = exception.getMessage();
        return message == null || message.isEmpty() ? exception.getClass().getSimpleName() : message;
    }
}
