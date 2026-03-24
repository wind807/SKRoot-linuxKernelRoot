package com.linux.permissionmanager.helper;

import static com.linux.permissionmanager.AppSettings.SHELL_SOCKET_NAME;

import android.content.Context;
import android.content.Intent;
import android.net.LocalServerSocket;
import android.net.LocalSocket;

import java.io.BufferedReader;
import java.io.BufferedWriter;
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

                // 超时线程：如果一直没有 accept 到连接，就主动关闭 server 打断 accept()
                Thread timeoutThread = new Thread(() -> {
                    try {
                        Thread.sleep(acceptTimeoutMs);
                        if (!accepted.get() && finished.compareAndSet(false, true)) {
                            try {
                                LocalServerSocket s = serverRef.get();
                                if (s != null) s.close();
                            } catch (Throwable ignore) {}
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
                // 如果不是已经超时/已经成功，才回调异常
                if (finished.compareAndSet(false, true)) {
                    callback.onResult("ERROR: " + t.getMessage());
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
        context.startService(new Intent(context, MagicaService.class));
    }
}