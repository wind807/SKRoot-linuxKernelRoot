package com.linux.permissionmanager.helper;

import static com.linux.permissionmanager.AppSettings.SHELL_SOCKET_NAME;

import android.content.Context;
import android.content.Intent;
import android.net.LocalServerSocket;
import android.net.LocalSocket;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.ResultReceiver;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

public class MagicaRootHelper {

    public interface ResultCallback {
        void onResult(String result);
    }

    public static void executeMagicaRootScript(Context context, String script, ResultCallback callback) {
        executeMagicaRootScript(context, script, 10000, callback);
    }

    public static void executeMagicaRootScript(Context context, String script, long acceptTimeoutMs, ResultCallback callback) {
        CountDownLatch latch = new CountDownLatch(1);
        AtomicBoolean finished = new AtomicBoolean(false);
        AtomicBoolean accepted = new AtomicBoolean(false);
        AtomicReference<LocalServerSocket> serverRef = new AtomicReference<>(null);

        new Thread(() -> {
            try (LocalServerSocket server = new LocalServerSocket(SHELL_SOCKET_NAME)) {
                serverRef.set(server);
                latch.countDown();

                // 超时线程
                Thread timeoutThread = new Thread(() -> {
                    try {
                        Thread.sleep(acceptTimeoutMs);
                        if (!accepted.get() && finished.compareAndSet(false, true)) {
                            closeServerQuietly(serverRef.get());
                            callback.onResult("ERROR: accept timeout");
                        }
                    } catch (InterruptedException ignored) {
                        Thread.currentThread().interrupt();
                    }
                });
                timeoutThread.start();

                try (LocalSocket client = server.accept();
                     BufferedWriter writer = new BufferedWriter(new OutputStreamWriter(client.getOutputStream()));
                     BufferedReader reader = new BufferedReader(new InputStreamReader(client.getInputStream()))) {
                    accepted.set(true);
                    writer.write(script);
                    writer.write("\n#__END__\n");
                    writer.flush();
                    StringBuilder result = new StringBuilder();
                    String line;
                    while ((line = reader.readLine()) != null) {
                        if ("#__END__".equals(line)) break;
                        result.append(line).append('\n');
                    }
                    if (finished.compareAndSet(false, true)) {
                        callback.onResult(result.toString());
                    }
                }

            } catch (Throwable t) {
                t.printStackTrace();
                latch.countDown();
                if (finished.compareAndSet(false, true)) {
                    callback.onResult("ERROR: Socket server error - " + t.getMessage());
                }
            }
        }).start();

        try {
            latch.await();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            if (finished.compareAndSet(false, true)) {
                callback.onResult("ERROR: interrupted");
            }
            return;
        }

        // 创建 ResultReceiver 接收 Service 内部状态
        ResultReceiver receiver = new ResultReceiver(new Handler(Looper.getMainLooper())) {
            @Override
            protected void onReceiveResult(int resultCode, Bundle resultData) {
                // -1 代表 Service 内部发生了异常
                if (resultCode == -1 && finished.compareAndSet(false, true)) {
                    String errorMsg = resultData != null ? resultData.getString("error") : "Unknown Service Error";
                    closeServerQuietly(serverRef.get());
                    callback.onResult("ERROR: Service internal failure - " + errorMsg);
                }
            }
        };

        Intent intent = new Intent(context, MagicaService.class);
        intent.putExtra("receiver", receiver);

        try {
            // 捕获系统拦截 (例如 IllegalStateException: Not allowed to start service)
            context.startService(intent);
        } catch (Exception e) {
            if (finished.compareAndSet(false, true)) {
                closeServerQuietly(serverRef.get());
                callback.onResult("ERROR: System intercepted startService - " + e.getMessage());
            }
        }
    }

    private static void closeServerQuietly(LocalServerSocket server) {
        if (server != null) {
            try {
                server.close();
            } catch (IOException ignored) {}
        }
    }
}