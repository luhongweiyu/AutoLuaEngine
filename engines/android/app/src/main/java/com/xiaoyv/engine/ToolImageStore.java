/**
 * 文件用途：保存 PC 工具上传的单次投影图片，并为投影 Activity 校验缓存文件标识。
 */
package com.xiaoyv.engine;

import android.content.Context;
import android.graphics.BitmapFactory;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.UUID;

/** PC 抓图取色工具的投影图片缓存。 */
final class ToolImageStore {
    private static final String DIRECTORY_NAME = "tool_projection";
    private static final long STALE_FILE_MS = 24L * 60L * 60L * 1000L;

    private ToolImageStore() {
    }

    /** 校验图片后写入应用缓存，返回只能由本类解析的随机文件标识。 */
    static String store(Context context, byte[] data) throws IOException {
        if (data == null || data.length == 0) {
            throw new IOException("投影图片为空");
        }
        BitmapFactory.Options bounds = new BitmapFactory.Options();
        bounds.inJustDecodeBounds = true;
        BitmapFactory.decodeByteArray(data, 0, data.length, bounds);
        if (bounds.outWidth <= 0 || bounds.outHeight <= 0) {
            throw new IOException("设备无法识别投影图片格式");
        }

        File directory = directory(context);
        cleanupStaleFiles(directory);
        String fileId = UUID.randomUUID().toString() + ".image";
        File target = new File(directory, fileId);
        try (FileOutputStream output = new FileOutputStream(target)) {
            output.write(data);
            output.flush();
        }
        return fileId;
    }

    /** 只允许访问投影缓存目录下一层随机文件，拒绝路径穿越和普通脚本文件。 */
    static File resolve(Context context, String fileId) throws IOException {
        if (fileId == null || !fileId.matches("[0-9a-fA-F-]+\\.image")) {
            throw new IOException("投影图片标识无效");
        }
        File directory = directory(context).getCanonicalFile();
        File file = new File(directory, fileId).getCanonicalFile();
        if (!file.getParentFile().equals(directory) || !file.isFile()) {
            throw new IOException("投影图片不存在");
        }
        return file;
    }

    private static File directory(Context context) throws IOException {
        File directory = new File(context.getCacheDir(), DIRECTORY_NAME);
        if (!directory.isDirectory() && !directory.mkdirs()) {
            throw new IOException("无法创建投影图片缓存目录");
        }
        return directory;
    }

    private static void cleanupStaleFiles(File directory) {
        File[] files = directory.listFiles();
        if (files == null) return;
        long cutoff = System.currentTimeMillis() - STALE_FILE_MS;
        for (File file : files) {
            if (file.isFile() && file.lastModified() < cutoff) file.delete();
        }
    }
}
