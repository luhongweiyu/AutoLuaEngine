/**
 * 文件用途：从 ALPKG ZIP 包读取未加密资源，并以 ContentProvider URI 提供给 WebView。
 */
package com.autolua.engine;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.content.res.AssetFileDescriptor;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.util.Base64;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import android.webkit.MimeTypeMap;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/**
 * ALPKG 资源内容提供器。
 *
 * Lua 字节码由 libengine.so 解密执行，Java 这里只读取 manifest 标记为 resource 的 ZIP
 * 文件。WebView 通过 content:// URI 加载 HTML、JS、CSS、图片，不会把资源解压到磁盘。
 */
public final class ScriptPackageResources extends ContentProvider {
    public static final String AUTHORITY = "com.autolua.engine.package";
    private static final String MIME_PACKAGE_RESOURCE = "application/octet-stream";
    private static final int MAX_RESOURCE_BYTES = 32 * 1024 * 1024;

    /**
     * 返回 WebView 可加载的包资源 URI。relativePath 必须是项目内相对路径。
     */
    public static Uri buildUri(String packagePath, String relativePath) {
        String encodedPackagePath = Base64.encodeToString(
                (packagePath == null ? "" : packagePath).getBytes(StandardCharsets.UTF_8),
                Base64.URL_SAFE | Base64.NO_WRAP | Base64.NO_PADDING
        );
        Uri.Builder builder = new Uri.Builder()
                .scheme("content")
                .authority(AUTHORITY)
                .appendPath("p")
                .appendPath(encodedPackagePath);
        String normalizedPath = relativePath == null ? "" : relativePath.replace('\\', '/');
        for (String segment : normalizedPath.split("/")) {
            if (!segment.isEmpty()) {
                builder.appendPath(segment);
            }
        }
        return builder.build();
    }

    /**
     * 从当前包读取资源文本，供旧 WebView 在加载 HTML 前注入 AutoLua bridge。
     */
    public static String readText(Context context, String packagePath, String relativePath)
            throws IOException {
        byte[] data = readResource(context, packagePath, relativePath);
        return new String(data, StandardCharsets.UTF_8);
    }

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public String getType(Uri uri) {
        String path = uri == null ? "" : uri.getLastPathSegment();
        String extension = MimeTypeMap.getFileExtensionFromUrl(path == null ? "" : path);
        String resolved = extension == null || extension.isEmpty()
                ? null
                : MimeTypeMap.getSingleton().getMimeTypeFromExtension(extension.toLowerCase());
        return resolved == null ? MIME_PACKAGE_RESOURCE : resolved;
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection, String[] selectionArgs, String sortOrder) {
        String[] columns = projection == null
                ? new String[] { OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE }
                : projection;
        MatrixCursor cursor = new MatrixCursor(columns);
        MatrixCursor.RowBuilder row = cursor.newRow();
        String fileName = uri == null || uri.getLastPathSegment() == null ? "" : uri.getLastPathSegment();
        for (String column : columns) {
            if (OpenableColumns.DISPLAY_NAME.equals(column)) {
                row.add(fileName);
            } else if (OpenableColumns.SIZE.equals(column)) {
                row.add(null);
            } else {
                row.add(null);
            }
        }
        return cursor;
    }

    @Override
    public AssetFileDescriptor openAssetFile(Uri uri, String mode) throws FileNotFoundException {
        if (uri == null) {
            throw new FileNotFoundException("资源 URI 为空");
        }
        try {
            Context context = getContext();
            if (context == null) {
                throw new IOException("脚本包资源提供器未初始化");
            }
            byte[] data = readResource(context, packagePath(uri), resourcePath(uri));
            ParcelFileDescriptor[] descriptors = ParcelFileDescriptor.createPipe();
            new Thread(() -> {
                try (OutputStream outputStream = new ParcelFileDescriptor.AutoCloseOutputStream(descriptors[1])) {
                    outputStream.write(data);
                } catch (IOException ignored) {
                    // WebView 关闭页面时可能主动关闭管道；此时无需重试。
                }
            }, "AlpkgResourcePipe").start();
            return new AssetFileDescriptor(descriptors[0], 0, data.length);
        } catch (IOException exception) {
            throw new FileNotFoundException(exception.getMessage());
        }
    }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        AssetFileDescriptor descriptor = openAssetFile(uri, mode);
        return descriptor.getParcelFileDescriptor();
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        return 0;
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) {
        return 0;
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        return null;
    }

    private static byte[] readResource(Context context, String packagePath, String relativePath)
            throws IOException {
        File packageFile = validatePackagePath(context, packagePath);
        String safePath = normalizeResourcePath(relativePath);
        try (ZipFile zipFile = new ZipFile(packageFile)) {
            JSONObject manifest = readManifest(zipFile);
            JSONObject files = manifest.optJSONObject("files");
            JSONObject metadata = files == null ? null : files.optJSONObject(safePath);
            if (metadata == null || !"resource".equals(metadata.optString("kind"))) {
                throw new FileNotFoundException("包内资源不存在：" + safePath);
            }

            String archivePath = metadata.optString("path", safePath);
            if (!safePath.equals(archivePath) || !isSafeRelativePath(archivePath)) {
                throw new IOException("包内资源索引无效");
            }
            ZipEntry entry = zipFile.getEntry(archivePath);
            if (entry == null || entry.isDirectory() || entry.getSize() > MAX_RESOURCE_BYTES) {
                throw new FileNotFoundException("包内资源不存在或过大：" + safePath);
            }
            try (InputStream inputStream = zipFile.getInputStream(entry)) {
                return readAllBytes(inputStream, MAX_RESOURCE_BYTES);
            }
        } catch (JSONException exception) {
            throw new IOException("脚本包 manifest 无效", exception);
        }
    }

    private static JSONObject readManifest(ZipFile zipFile) throws IOException, JSONException {
        ZipEntry entry = zipFile.getEntry("manifest.json");
        if (entry == null || entry.getSize() > 1024 * 1024) {
            throw new IOException("脚本包缺少 manifest");
        }
        try (InputStream inputStream = zipFile.getInputStream(entry)) {
            JSONObject manifest = new JSONObject(new String(readAllBytes(inputStream, 1024 * 1024), StandardCharsets.UTF_8));
            if (!"alpkg".equals(manifest.optString("format")) || manifest.optInt("formatVersion") != 1) {
                throw new IOException("脚本包格式不受支持");
            }
            return manifest;
        }
    }

    private static File validatePackagePath(Context context, String packagePath) throws FileNotFoundException {
        if (packagePath == null || packagePath.trim().isEmpty()) {
            throw new FileNotFoundException("脚本包路径为空");
        }
        try {
            File packageFile = new File(packagePath).getCanonicalFile();
            File storageRoot = android.os.Environment.getExternalStorageDirectory().getCanonicalFile();
            String prefix = storageRoot.getPath() + File.separator;
            if (!packageFile.isFile() || !packageFile.getPath().startsWith(prefix)
                    || !packageFile.getName().toLowerCase().endsWith(".alpkg")) {
                throw new FileNotFoundException("脚本包路径无效");
            }
            return packageFile;
        } catch (IOException exception) {
            throw new FileNotFoundException("脚本包路径无效");
        }
    }

    private static String packagePath(Uri uri) throws IOException {
        if (uri == null || uri.getPathSegments().size() < 3
                || !"p".equals(uri.getPathSegments().get(0))) {
            throw new IOException("脚本包资源 URI 无效");
        }
        try {
            byte[] decoded = Base64.decode(uri.getPathSegments().get(1), Base64.URL_SAFE | Base64.NO_PADDING);
            String packagePath = new String(decoded, StandardCharsets.UTF_8);
            if (packagePath.isEmpty()) {
                throw new IOException("脚本包路径为空");
            }
            return packagePath;
        } catch (IllegalArgumentException exception) {
            throw new IOException("脚本包资源 URI 无效", exception);
        }
    }

    private static String resourcePath(Uri uri) throws IOException {
        if (uri == null || uri.getPathSegments().size() < 3
                || !"p".equals(uri.getPathSegments().get(0))) {
            throw new IOException("包内资源路径为空");
        }
        StringBuilder path = new StringBuilder();
        for (int index = 2; index < uri.getPathSegments().size(); index++) {
            if (path.length() > 0) {
                path.append('/');
            }
            path.append(uri.getPathSegments().get(index));
        }
        return normalizeResourcePath(path.toString());
    }

    private static String normalizeResourcePath(String path) throws IOException {
        String normalized = path == null ? "" : path.replace('\\', '/');
        if (!isSafeRelativePath(normalized)) {
            throw new IOException("包内资源路径无效");
        }
        return normalized;
    }

    private static boolean isSafeRelativePath(String path) {
        if (path == null || path.isEmpty() || path.startsWith("/") || path.indexOf('\\') >= 0) {
            return false;
        }
        String[] parts = path.split("/");
        for (String part : parts) {
            if (part.isEmpty() || ".".equals(part) || "..".equals(part)) {
                return false;
            }
        }
        return true;
    }

    private static byte[] readAllBytes(InputStream inputStream, int maxBytes) throws IOException {
        try (ByteArrayOutputStream outputStream = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[8192];
            int count;
            while ((count = inputStream.read(buffer)) != -1) {
                if (outputStream.size() > maxBytes - count) {
                    throw new IOException("包内资源超过大小限制");
                }
                outputStream.write(buffer, 0, count);
            }
            return outputStream.toByteArray();
        }
    }
}
