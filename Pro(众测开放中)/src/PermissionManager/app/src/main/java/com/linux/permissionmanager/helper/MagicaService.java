package com.linux.permissionmanager.helper;

import static com.linux.permissionmanager.AppSettings.SHELL_SOCKET_NAME;

import android.app.Service;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.os.Bundle;
import android.os.IBinder;
import android.os.ResultReceiver;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;

import androidx.annotation.Nullable;

import com.linux.permissionmanager.utils.FileUtils;
import com.linux.permissionmanager.utils.ShellUtils;

import java.io.BufferedReader;
import java.io.File;
import java.io.InputStreamReader;

public class MagicaService extends Service {
    private static final String TAG = "Magica";

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        ResultReceiver receiver = null;
        if (intent != null) receiver = intent.getParcelableExtra("receiver");

        try {
            Os.setuid(0);
        } catch (ErrnoException e) {
            e.printStackTrace();
            if (receiver != null) {
                Bundle bundle = new Bundle();
                bundle.putString("error", "setuid(0) failed: " + e.getMessage());
                receiver.send(-1, bundle);
            }
            return START_NOT_STICKY; // 提权失败，没必要继续了
        } catch (Exception e) {
            if (receiver != null) {
                Bundle bundle = new Bundle();
                bundle.putString("error", e.getMessage());
                receiver.send(-1, bundle);
            }
            return START_NOT_STICKY;
        }

        // 走到这里说明没有异常，启动客户端线程
        startClient(receiver);
        return START_STICKY;
    }

    private void startClient(ResultReceiver receiver) {
        new Thread(() -> {
            try {
                LocalSocket socket = new LocalSocket();
                socket.connect(new LocalSocketAddress(SHELL_SOCKET_NAME));
                Log.d(TAG, "Connected to server!");

                // 成功连接，可以通知 Helper 一切顺利
                if (receiver != null) receiver.send(1, null);

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

                java.io.BufferedWriter writer = new java.io.BufferedWriter(new java.io.OutputStreamWriter(socket.getOutputStream()));
                writer.write(result);
                writer.write("\n#__END__\n");
                writer.flush();
                socket.close();
            } catch (Throwable t) {
                Log.e(TAG, "Client error", t);
                if (receiver != null) {
                    Bundle bundle = new Bundle();
                    bundle.putString("error", "Socket client error: " + t.getMessage());
                    receiver.send(-1, bundle);
                }
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