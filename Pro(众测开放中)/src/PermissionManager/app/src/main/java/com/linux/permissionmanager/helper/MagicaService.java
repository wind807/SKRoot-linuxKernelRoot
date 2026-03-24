package com.linux.permissionmanager.helper;

import static com.linux.permissionmanager.AppSettings.SHELL_SOCKET_NAME;

import android.app.Service;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.os.IBinder;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;

import androidx.annotation.Nullable;

import com.linux.permissionmanager.AppSettings;
import com.linux.permissionmanager.utils.FileUtils;
import com.linux.permissionmanager.utils.ShellUtils;

import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.InputStreamReader;

public class MagicaService extends Service {
    private static final String TAG = "Magica";

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        try {
            Os.setuid(0);
        } catch (ErrnoException e) {
            e.printStackTrace();
        }
        startClient();
        return START_STICKY;
    }

    private void startClient() {
        new Thread(() -> {
            try {
                LocalSocket socket = new LocalSocket();
                socket.connect(new LocalSocketAddress(SHELL_SOCKET_NAME));
                Log.d(TAG, "Connected to server!");
                BufferedReader reader = new BufferedReader(new InputStreamReader(socket.getInputStream()));
                StringBuilder script = new StringBuilder();
                String line;

                while ((line = reader.readLine()) != null) {
                    if ("#__END__".equals(line)) break;
                    script.append(line).append('\n');
                }

                createResetpropTools();
                Log.d(TAG, "Received script length=" + script.length());
                String result = ShellUtils.executeScript(script.toString(), "/data/adb/1.h");
                Log.d(TAG, "Execute result:\n" + result);
                deleteResetpropTools();

                // 把结果写回去
                java.io.BufferedWriter writer = new java.io.BufferedWriter(new java.io.OutputStreamWriter(socket.getOutputStream()));
                writer.write(result);
                writer.write("\n#__END__\n");
                writer.flush();
                socket.close();
            } catch (Throwable t) {
                Log.e(TAG, "Client error", t);
            }
        }).start();
    }

    @Nullable
    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    private void createResetpropTools() {
        ApplicationInfo appInfo = getApplicationInfo();
        File srcFile = new File(appInfo.nativeLibraryDir, "libresetprop.so");
        File dstFile = new File("/data/adb/resetprop");
        FileUtils.copyFile(srcFile, dstFile);
    }

    private void deleteResetpropTools() {
        File dstFile = new File("/data/adb/resetprop");
        FileUtils.deleteFile(dstFile);
    }
}