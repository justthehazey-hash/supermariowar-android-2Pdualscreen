package org.supermariowar.app;

import android.content.Context;
import android.os.Build;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.PrintWriter;
import java.io.ByteArrayOutputStream;
import java.io.FileInputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.Deque;

/**
 * Crash reports without a computer: the game logs to a file (native side:
 * src/smw/AndroidCrashLog.cpp), crashes are appended to it, and the next launch
 * offers the report for copying.
 */
final class CrashReport {
    private static final String LOG = "last-run.log";
    private static final String KEPT = "last-crash.log";
    private static boolean installed;

    private CrashReport() {}

    static File logFile(Context context) {
        return new File(context.getFilesDir(), LOG);
    }

    /** Call before the game starts: tells native code where to log, records Java crashes. */
    static void install(Context context) {
        File log = logFile(context);
        try {
            android.system.Os.setenv("SMW_LOG", log.getAbsolutePath(), true);
        } catch (Exception ex) {
            android.util.Log.w("SMW", "Cannot set SMW_LOG", ex);
        }
        if (installed) return;
        installed = true;
        Thread.UncaughtExceptionHandler previous = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((thread, error) -> {
            try (PrintWriter out = new PrintWriter(new FileOutputStream(log, true))) {
                out.println();
                out.println("*** JAVA CRASH in thread " + thread.getName());
                error.printStackTrace(out);
            } catch (IOException ignored) {
            }
            if (previous != null) previous.uncaughtException(thread, error);
        });
    }

    /** The last run's crash report (tail of its log), or null if it did not crash. */
    static String take(Context context) {
        File log = logFile(context);
        if (!log.isFile()) return null;
        String text;
        try (InputStream in = new FileInputStream(log)) {
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            byte[] buffer = new byte[16384];
            int count;
            while ((count = in.read(buffer)) != -1) bytes.write(buffer, 0, count);
            text = new String(bytes.toByteArray(), StandardCharsets.UTF_8);
        } catch (IOException ex) {
            return null;
        }
        if (!text.contains("*** CRASH") && !text.contains("*** JAVA CRASH")
                && !text.contains("*** Uncaught")) return null;
        // Keep a copy, and make sure the same crash is only reported once.
        File kept = new File(context.getFilesDir(), KEPT);
        kept.delete();
        if (!log.renameTo(kept)) log.delete();

        Deque<String> tail = new ArrayDeque<>();
        for (String line : text.split("\n")) {
            tail.addLast(line);
            if (tail.size() > 90) tail.removeFirst();
        }
        StringBuilder report = new StringBuilder();
        report.append("Super Mario War ").append(BuildConfig.VERSION_NAME)
              .append(" (").append(BuildConfig.VERSION_CODE).append(")\n")
              .append(Build.MANUFACTURER).append(' ').append(Build.MODEL)
              .append(", Android ").append(Build.VERSION.RELEASE)
              .append(" (API ").append(Build.VERSION.SDK_INT).append(")\n\n");
        for (String line : tail) report.append(line).append('\n');
        return report.toString();
    }
}
