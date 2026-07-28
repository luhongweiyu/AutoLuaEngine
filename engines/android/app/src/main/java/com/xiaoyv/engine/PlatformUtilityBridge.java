/**
 * 文件用途：实现音频、媒体扫描、ZIP 与 APK assets 提取等平台辅助能力。
 */
package com.xiaoyv.engine;

import android.content.Context;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.res.AssetManager;
import android.media.MediaPlayer;
import android.media.MediaScannerConnection;

import org.json.JSONObject;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.Charset;
import java.util.regex.Pattern;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;

import net.lingala.zip4j.ZipFile;
import net.lingala.zip4j.model.FileHeader;

final class PlatformUtilityBridge {
    private static final Object AUDIO_LOCK = new Object();
    private static MediaPlayer mediaPlayer;

    private PlatformUtilityBridge() {
    }

    static Object call(Context context, String operation, JSONObject arguments) throws Exception {
        switch (operation) {
            case "media.playAudio":
                playAudio(requireText(arguments, "path"));
                return true;
            case "media.stopAudio":
                stopAudio();
                return true;
            case "media.scanImage":
                scanImage(context, requireText(arguments, "path"));
                return true;
            case "file.zip":
                zip(new File(requireText(arguments, "source")), new File(requireText(arguments, "zip")));
                return true;
            case "file.unzip":
                return unzip(arguments);
            case "file.extractAsset":
                return extractAsset(
                        context,
                        requireText(arguments, "asset"),
                        new File(requireText(arguments, "output"))
                );
            case "file.extractAssetArchive":
                return extractAssetArchive(context, arguments);
            case "device.isDebug":
                return (context.getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) != 0;
            case "device.setDisplayDensity":
                setDisplayDensity(arguments.getInt("dpi"));
                return true;
            case "device.resetDisplayDensity":
                resetDisplayDensity();
                return true;
            case "device.showControlBar":
                showControlBar(context, arguments.optBoolean("show", true));
                return true;
            case "device.setControlBarPosition":
                setControlBarPosition(
                        context,
                        arguments.optDouble("x", 0.5),
                        arguments.optDouble("y", 0.5)
                );
                return true;
            case "device.restartScript":
                EngineService.restartScript(context);
                return true;
            default:
                throw new IllegalArgumentException("不支持的平台辅助能力：" + operation);
        }
    }

    private static void playAudio(String path) throws IOException {
        synchronized (AUDIO_LOCK) {
            stopAudio();
            MediaPlayer player = new MediaPlayer();
            try {
                player.setDataSource(path);
                player.setOnCompletionListener(completed -> {
                    synchronized (AUDIO_LOCK) {
                        if (mediaPlayer == completed) {
                            mediaPlayer = null;
                        }
                        completed.release();
                    }
                });
                player.prepare();
                player.start();
                mediaPlayer = player;
            } catch (IOException | RuntimeException exception) {
                player.release();
                throw exception;
            }
        }
    }

    private static void stopAudio() {
        synchronized (AUDIO_LOCK) {
            if (mediaPlayer == null) {
                return;
            }
            try {
                mediaPlayer.stop();
            } catch (IllegalStateException ignored) {
                // 已结束的播放器仍然需要 release。
            }
            mediaPlayer.release();
            mediaPlayer = null;
        }
    }

    private static void scanImage(Context context, String path) {
        MediaScannerConnection.scanFile(
                context,
                new String[]{path},
                null,
                null
        );
    }

    private static void zip(File source, File target) throws IOException {
        if (!source.exists()) {
            throw new IllegalArgumentException("待压缩文件不存在：" + source);
        }
        File canonicalSource = source.getCanonicalFile();
        File canonicalTarget = target.getCanonicalFile();
        if (canonicalSource.equals(canonicalTarget)) {
            throw new IllegalArgumentException("ZIP 输出文件不能与待压缩文件相同");
        }
        if (canonicalSource.isDirectory()
                && canonicalTarget.getPath().startsWith(
                    canonicalSource.getPath() + File.separator
                )) {
            throw new IllegalArgumentException("ZIP 输出文件不能放在待压缩目录内部");
        }
        File parent = target.getParentFile();
        if (parent != null && !parent.exists() && !parent.mkdirs()) {
            throw new IOException("无法创建 ZIP 目录：" + parent);
        }
        String rootName = source.getName();
        try (ZipOutputStream output = new ZipOutputStream(
                new BufferedOutputStream(new FileOutputStream(target, false))
        )) {
            addZipEntry(output, source, source.isDirectory() ? rootName + "/" : rootName);
        }
    }

    private static void addZipEntry(ZipOutputStream output, File source, String entryName)
            throws IOException {
        String normalized = entryName.replace(File.separatorChar, '/');
        if (source.isDirectory()) {
            if (!normalized.endsWith("/")) {
                normalized += "/";
            }
            output.putNextEntry(new ZipEntry(normalized));
            output.closeEntry();
            File[] children = source.listFiles();
            if (children == null) {
                return;
            }
            java.util.Arrays.sort(children, (left, right) -> left.getName().compareTo(right.getName()));
            for (File child : children) {
                addZipEntry(output, child, normalized + child.getName());
            }
            return;
        }

        output.putNextEntry(new ZipEntry(normalized));
        try (InputStream input = new BufferedInputStream(new FileInputStream(source))) {
            copy(input, output);
        }
        output.closeEntry();
    }

    private static int unzip(JSONObject arguments) throws Exception {
        String password = arguments.optString("password", "");
        Charset charset;
        try {
            charset = Charset.forName(arguments.optString("charset", "UTF-8"));
        } catch (Exception exception) {
            throw new IllegalArgumentException("ZIP 文件名编码无效");
        }
        File archive = new File(requireText(arguments, "zip"));
        File output = new File(requireText(arguments, "output"));
        if (!archive.isFile()) {
            throw new IllegalArgumentException("ZIP 文件不存在：" + archive);
        }
        if (!output.exists() && !output.mkdirs()) {
            throw new IOException("无法创建解压目录：" + output);
        }

        ZipFile zipFile = new ZipFile(
                archive,
                password.isEmpty() ? null : password.toCharArray()
        );
        zipFile.setCharset(charset);
        if (zipFile.isEncrypted() && password.isEmpty()) {
            throw new IllegalArgumentException("ZIP 已加密，必须提供密码");
        }

        int count = 0;
        for (FileHeader header : zipFile.getFileHeaders()) {
            File target = new File(output, header.getFileName());
            ensureInsideOrRoot(output, target);
            if (!header.isDirectory()) {
                count++;
            }
        }
        try {
            zipFile.extractAll(output.getAbsolutePath());
        } catch (net.lingala.zip4j.exception.ZipException exception) {
            throw new IOException("ZIP 解压失败：" + exception.getMessage(), exception);
        }
        return count;
    }

    private static String extractAsset(Context context, String assetPath, File outputDirectory)
            throws IOException {
        if (!outputDirectory.exists() && !outputDirectory.mkdirs()) {
            throw new IOException("无法创建 assets 输出目录：" + outputDirectory);
        }
        File target = new File(outputDirectory, new File(assetPath).getName());
        ensureInside(outputDirectory, target);
        try (InputStream input = context.getAssets().open(assetPath);
             FileOutputStream output = new FileOutputStream(target, false)) {
            copy(input, output);
        }
        return target.getAbsolutePath();
    }

    private static int extractAssetArchive(Context context, JSONObject arguments)
            throws IOException {
        String assetPath = requireText(arguments, "asset");
        File outputDirectory = new File(requireText(arguments, "output"));
        String glob = arguments.optString("pattern", "*");
        if (!outputDirectory.exists() && !outputDirectory.mkdirs()) {
            throw new IOException("无法创建 assets 输出目录：" + outputDirectory);
        }
        Pattern pattern = globPattern(glob);
        int extracted = 0;
        AssetManager assets = context.getAssets();
        try (ZipInputStream input = new ZipInputStream(
                new BufferedInputStream(assets.open(assetPath))
        )) {
            ZipEntry entry;
            while ((entry = input.getNextEntry()) != null) {
                String name = entry.getName().replace('\\', '/');
                if (entry.isDirectory() || !pattern.matcher(new File(name).getName()).matches()) {
                    input.closeEntry();
                    continue;
                }
                File target = new File(outputDirectory, name);
                ensureInside(outputDirectory, target);
                File parent = target.getParentFile();
                if (parent != null && !parent.exists() && !parent.mkdirs()) {
                    throw new IOException("无法创建 assets 输出目录：" + parent);
                }
                try (FileOutputStream output = new FileOutputStream(target, false)) {
                    copy(input, output);
                }
                extracted++;
                input.closeEntry();
            }
        }
        return extracted;
    }

    private static Pattern globPattern(String glob) {
        StringBuilder regex = new StringBuilder("^");
        for (int index = 0; index < glob.length(); index++) {
            char value = glob.charAt(index);
            if (value == '*') {
                regex.append(".*");
            } else if (value == '?') {
                regex.append('.');
            } else {
                regex.append(Pattern.quote(String.valueOf(value)));
            }
        }
        return Pattern.compile(regex.append('$').toString(), Pattern.CASE_INSENSITIVE);
    }

    private static void ensureInside(File root, File target) throws IOException {
        String rootPath = root.getCanonicalPath() + File.separator;
        String targetPath = target.getCanonicalPath();
        if (!targetPath.startsWith(rootPath)) {
            throw new IOException("输出路径越界：" + target);
        }
    }

    private static void ensureInsideOrRoot(File root, File target) throws IOException {
        String rootPath = root.getCanonicalPath();
        String targetPath = target.getCanonicalPath();
        if (!targetPath.equals(rootPath)
                && !targetPath.startsWith(rootPath + File.separator)) {
            throw new IOException("ZIP 包含越界路径：" + target);
        }
    }

    private static void setDisplayDensity(int dpi) {
        if (dpi < 72 || dpi > 1000) {
            throw new IllegalArgumentException("dpi 必须在 72 到 1000 之间");
        }
        RootHelperBridge.ShellResult result = RootHelperBridge.executeShell(
                "wm density " + dpi
        );
        if (!result.success) {
            throw new IllegalArgumentException(result.error);
        }
    }

    private static void resetDisplayDensity() {
        RootHelperBridge.ShellResult result = RootHelperBridge.executeShell("wm density reset");
        if (!result.success) {
            throw new IllegalArgumentException(result.error);
        }
    }

    private static void showControlBar(Context context, boolean show) {
        if (!show) {
            context.stopService(new Intent(context, FloatingControlService.class));
            return;
        }
        Intent intent = new Intent(context, FloatingControlService.class);
        intent.setAction(FloatingControlService.ACTION_SHOW);
        context.startService(intent);
    }

    private static void setControlBarPosition(Context context, double x, double y) {
        if (!Double.isFinite(x) || !Double.isFinite(y) || x < 0 || x > 1 || y < 0 || y > 1) {
            throw new IllegalArgumentException("控制条位置必须是 0 到 1 之间的比例");
        }
        android.util.DisplayMetrics metrics = context.getResources().getDisplayMetrics();
        EngineSettings.setFloatingBubblePosition(
                context,
                (int) Math.round(x * Math.max(0, metrics.widthPixels - 1)),
                (int) Math.round(y * Math.max(0, metrics.heightPixels - 1))
        );
        showControlBar(context, true);
    }

    private static String requireText(JSONObject arguments, String name) {
        String value = arguments.optString(name, "");
        if (value.isEmpty()) {
            throw new IllegalArgumentException(name + " 参数不能为空");
        }
        return value;
    }

    private static void copy(InputStream input, java.io.OutputStream output) throws IOException {
        byte[] buffer = new byte[32 * 1024];
        int count;
        while ((count = input.read(buffer)) != -1) {
            output.write(buffer, 0, count);
        }
    }
}
