package com.linux.permissionmanager.utils;

import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

public class FileUtils {
    public static String getPathFromUriByCopy(Context context, Uri uri) {
        if (uri == null) return null;
        String fileName = getFileNameFromUri(context, uri);
        if (fileName == null || fileName.isEmpty()) {
            fileName = "temp_file_" + System.currentTimeMillis() + ".zip";
        }
        File cacheDir = context.getCacheDir();
        File tempFile = new File(cacheDir, fileName);
        try (InputStream inputStream = context.getContentResolver().openInputStream(uri);
             FileOutputStream outputStream = new FileOutputStream(tempFile)) {
            if (inputStream == null) return null;
            byte[] buffer = new byte[8192];
            int length;
            while ((length = inputStream.read(buffer)) > 0) {
                outputStream.write(buffer, 0, length);
            }
            outputStream.flush();
            return tempFile.getAbsolutePath();
        } catch (Exception e) {
            e.printStackTrace();
            return null;
        }
    }

    /**
     * 从 Uri 中提取真实文件名
     */
    private static String getFileNameFromUri(Context context, Uri uri) {
        String result = null;
        if ("content".equals(uri.getScheme())) {
            try (Cursor cursor = context.getContentResolver().query(uri, null, null, null, null)) {
                if (cursor != null && cursor.moveToFirst()) {
                    int index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                    if (index >= 0) {
                        result = cursor.getString(index);
                    }
                }
            } catch (Exception e) {
                e.printStackTrace();
            }
        }
        if (result == null) {
            result = uri.getPath();
            if (result != null) {
                int cut = result.lastIndexOf('/');
                if (cut != -1) {
                    result = result.substring(cut + 1);
                }
            }
        }
        return result;
    }

    public static boolean copyFile(File src, File dst) {
        if (src == null || !src.exists() || !src.isFile()) return false;
        File parent = dst.getParentFile();
        if (parent != null && !parent.exists() && !parent.mkdirs()) return false;

        try (InputStream in = new FileInputStream(src);
             FileOutputStream out = new FileOutputStream(dst)) {
            byte[] buf = new byte[8192];
            int len;
            while ((len = in.read(buf)) > 0) {
                out.write(buf, 0, len);
            }
            out.flush();
            return true;
        } catch (IOException e) {
            return false;
        }
    }

    public static void deleteFile(File destFile) {
        try {
            if (destFile != null && destFile.exists()) destFile.delete();
        } catch (Exception ignore) {}
    }

    public static String formatFileSize(long size) {
        // 定义单位
        final long K = 1024;
        final long M = K * 1024;
        final long G = M * 1024;
        if (size >= G) {
            // 大于或等于GB
            return String.format("%.2f GB", (double) size / G);
        } else if (size >= M) {
            // 大于或等于MB
            return String.format("%.2f MB", (double) size / M);
        } else if (size >= K) {
            // 大于或等于KB
            return String.format("%.2f KB", (double) size / K);
        } else {
            // 小于KB
            return size + " B";
        }
    }

    public static String readFile(File file) throws Exception {
        try (FileInputStream fis = new FileInputStream(file);
             ByteArrayOutputStream bos = new ByteArrayOutputStream()) {
            byte[] buf = new byte[4096];
            int len;
            while ((len = fis.read(buf)) != -1) {
                bos.write(buf, 0, len);
            }
            return bos.toString(StandardCharsets.UTF_8.name());
        }
    }

    public static String readTextFile(String path) {
        java.io.File file = new java.io.File(path);
        if (!file.exists() || !file.isFile()) return null;
        StringBuilder sb = new StringBuilder();
        try (java.io.BufferedReader br = new java.io.BufferedReader(new java.io.InputStreamReader(new java.io.FileInputStream(file)))) {
            String line;
            while ((line = br.readLine()) != null) {
                sb.append(line).append('\n');
            }
        } catch (Exception e) {
            e.printStackTrace();
            return null;
        }
        return sb.toString();
    }
}
