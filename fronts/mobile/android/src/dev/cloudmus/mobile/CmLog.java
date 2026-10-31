package dev.cloudmus.mobile;

import android.Manifest;
import android.app.ActivityManager;
import android.app.Application;
import android.app.ApplicationExitInfo;
import android.content.ContentValues;
import android.content.Context;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Process;
import android.os.SystemClock;
import android.provider.MediaStore;
import android.util.DisplayMetrics;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

// The app's log, for debugging in the car afterwards: every layer of both
// processes (Java here, C++ through Logging::AndroidLog) writes through it.
// Lines go to logcat and to a file per process in files/logs — the one the
// Log page reads — mirrored where a file manager finds them: the app's
// external files dir and Download/Cloudmus/logs. A mirror that isn't there
// yet (storage not mounted this early after boot, no permission) is caught
// up with the whole file once it is, so nothing from the start is lost.
// Line format: Logging::formatLine() on the C++ side.
public final class CmLog {
    private static final String TAG = "cloudmus";
    private static final String START_TAG = "CloudmusStart";
    private static final long MAX_FILE_BYTES = 5L * 1024 * 1024;
    private static final int MAX_FILES_PER_PROCESS = 10;
    private static final long FLUSH_INTERVAL_MS = 1000;

    private static final Object lock = new Object();
    private static final SimpleDateFormat lineTime = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.US);
    private static final StringBuilder pending = new StringBuilder();

    private static Context context;
    private static String process = "?";
    private static long processStartElapsed;
    private static Handler writer;
    private static File internalDir;
    private static File internalFile;
    private static final List<Mirror> mirrors = new ArrayList<>();
    private static boolean flushScheduled;

    private static final class Mirror {
        final String name;
        final File dir;
        // Holds everything internalFile does.
        boolean synced;

        Mirror(String name, File dir) {
            this.name = name;
            this.dir = dir;
        }
    }

    private CmLog() {
    }

    // From Application.onCreate(), in each process.
    public static void init(Context appContext) {
        synchronized (lock) {
            if (context != null)
                return;
            context = appContext.getApplicationContext();
            String name = processName();
            process = name.endsWith(":playback") ? "playback" : "ui";
            processStartElapsed = Build.VERSION.SDK_INT >= 24 ? Process.getStartElapsedRealtime()
                                                               : SystemClock.elapsedRealtime();
            internalDir = new File(context.getFilesDir(), "logs");
            internalDir.mkdirs();
            internalFile = newFile(internalDir);
            File external = context.getExternalFilesDir(null);
            if (external != null)
                mirrors.add(new Mirror("app external", new File(external, "logs")));
            mirrors.add(new Mirror("Download",
                new File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS), "Cloudmus/logs")));
            HandlerThread thread = new HandlerThread("log-writer");
            thread.start();
            writer = new Handler(thread.getLooper());
        }
        Thread.UncaughtExceptionHandler previous = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((thread, error) -> {
            write(Log.ERROR, "crash", "uncaught in " + thread.getName() + ": " + stackTrace(error));
            flushNow();
            if (previous != null)
                previous.uncaughtException(thread, error);
        });
        writeHeader();
        checkPreviousExit();
        mark("process started (" + processName() + ")");
    }

    static Context context() {
        return context;
    }

    public static String processTag() {
        return process;
    }

    public static File internalDirectory() {
        return internalDir;
    }

    // Where a file manager finds the log; null before init.
    public static File publicDirectory() {
        for (Mirror mirror : mirrors) {
            if (mirror.name.equals("Download"))
                return mirror.dir;
        }
        return null;
    }

    // `priority`: android.util.Log's.
    public static void write(int priority, String tag, String message) {
        Log.println(priority, tag, message);
        if (writer == null)
            return;
        String line = format(priority, tag, message);
        boolean urgent = priority >= Log.WARN;
        synchronized (lock) {
            pending.append(line);
            if (urgent || !flushScheduled) {
                flushScheduled = true;
                writer.removeCallbacks(flusher);
                writer.postDelayed(flusher, urgent ? 0 : FLUSH_INTERVAL_MS);
            }
        }
    }

    public static void d(String tag, String message) {
        write(Log.DEBUG, tag, message);
    }

    public static void i(String tag, String message) {
        write(Log.INFO, tag, message);
    }

    public static void w(String tag, String message) {
        write(Log.WARN, tag, message);
    }

    public static void e(String tag, String message, Throwable error) {
        write(Log.ERROR, tag, error == null ? message : message + ": " + stackTrace(error));
    }

    // A startup milestone: with the time since the process started and since
    // the system booted, in the log and under CloudmusStart in logcat.
    public static void mark(String what) {
        long now = SystemClock.elapsedRealtime();
        String message = String.format(Locale.US, "%s (+%d ms since process start, %.3f s since boot)", what,
            now - processStartElapsed, now / 1000.0);
        Log.i(START_TAG, process + ": " + message);
        write(Log.INFO, "Startup", message);
        synchronized (lock) {
            if (writer != null) {
                writer.removeCallbacks(flusher);
                writer.post(flusher);
            }
        }
    }

    // The process is going away on its own accord: not a crash next time.
    public static void cleanExit() {
        i(TAG, "clean exit");
        runningMarker().delete();
        flushNow();
    }

    // Storage permission was granted: mirrors may work now.
    public static void storageChanged() {
        i(TAG, "storage permission changed");
        writer.post(flusher);
    }

    // Writes out what's pending, on the calling thread (a crash, an exit).
    public static void flushNow() {
        flusher.run();
    }

    // Packs every log file into a zip in Download/Cloudmus; where it went, or
    // what went wrong, for the user.
    public static String exportZip() {
        flushNow();
        String name = "cloudmus-logs-" + new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(new Date()) + ".zip";
        File[] files = internalDir.listFiles((dir, file) -> file.endsWith(".log"));
        if (files == null || files.length == 0)
            return "No log files";
        Arrays.sort(files);
        try {
            if (Build.VERSION.SDK_INT >= 29) {
                ContentValues values = new ContentValues();
                values.put(MediaStore.Downloads.DISPLAY_NAME, name);
                values.put(MediaStore.Downloads.MIME_TYPE, "application/zip");
                values.put(MediaStore.Downloads.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS + "/Cloudmus");
                Uri uri = context.getContentResolver().insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
                if (uri == null)
                    throw new IOException("MediaStore refused the file");
                try (OutputStream out = context.getContentResolver().openOutputStream(uri)) {
                    zip(files, out);
                }
                i(TAG, "log exported to Download/Cloudmus/" + name);
                return "Saved to Download/Cloudmus/" + name;
            }
            File dir = new File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS), "Cloudmus");
            if (!canWriteShared())
                dir = context.getExternalFilesDir(null);
            if (dir == null)
                throw new IOException("no external storage");
            dir.mkdirs();
            File target = new File(dir, name);
            try (OutputStream out = new FileOutputStream(target)) {
                zip(files, out);
            }
            i(TAG, "log exported to " + target);
            return "Saved to " + target;
        } catch (IOException | RuntimeException error) {
            e(TAG, "log export failed", error);
            return "Export failed: " + error.getMessage();
        }
    }

    private static void zip(File[] files, OutputStream out) throws IOException {
        try (ZipOutputStream zip = new ZipOutputStream(out)) {
            byte[] buffer = new byte[64 * 1024];
            for (File file : files) {
                zip.putNextEntry(new ZipEntry(file.getName()));
                try (InputStream in = new FileInputStream(file)) {
                    int n;
                    while ((n = in.read(buffer)) > 0)
                        zip.write(buffer, 0, n);
                }
                zip.closeEntry();
            }
        }
    }

    private static final Runnable flusher = () -> {
        String chunk;
        synchronized (lock) {
            flushScheduled = false;
            if (pending.length() == 0 && allMirrorsSynced())
                return;
            chunk = pending.toString();
            pending.setLength(0);
            byte[] bytes = chunk.getBytes(StandardCharsets.UTF_8);
            if (internalFile.length() + bytes.length > MAX_FILE_BYTES) {
                internalFile = newFile(internalDir);
                for (Mirror mirror : mirrors)
                    mirror.synced = false;
            }
            append(internalFile, bytes);
            for (Mirror mirror : mirrors) {
                File target = new File(mirror.dir, internalFile.getName());
                if (mirror.synced) {
                    if (!append(target, bytes))
                        mirror.synced = false;
                } else if (mirrorAvailable(mirror)) {
                    // Catching up: the whole file, this chunk included.
                    mirror.synced = copy(internalFile, target);
                    if (mirror.synced) {
                        prune(mirror.dir);
                        Log.i(TAG, "log mirrored to " + mirror.dir);
                    }
                }
            }
        }
    };

    private static boolean allMirrorsSynced() {
        for (Mirror mirror : mirrors) {
            if (!mirror.synced && mirrorAvailable(mirror))
                return false;
        }
        return true;
    }

    private static boolean mirrorAvailable(Mirror mirror) {
        if (!Environment.MEDIA_MOUNTED.equals(Environment.getExternalStorageState()))
            return false;
        if (mirror.name.equals("Download") && !canWriteShared())
            return false;
        return mirror.dir.isDirectory() || mirror.dir.mkdirs();
    }

    // Plain files in Download: Android 8-9 with the permission, 10 with
    // legacy storage too; from 11 only the zip export (MediaStore) gets there.
    private static boolean canWriteShared() {
        if (Build.VERSION.SDK_INT >= 30)
            return false;
        return context.checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE)
            == PackageManager.PERMISSION_GRANTED;
    }

    private static boolean append(File file, byte[] bytes) {
        try (FileOutputStream out = new FileOutputStream(file, true)) {
            out.write(bytes);
            return true;
        } catch (IOException error) {
            Log.w(TAG, "can't write " + file + ": " + error);
            return false;
        }
    }

    private static boolean copy(File from, File to) {
        try (InputStream in = new FileInputStream(from); OutputStream out = new FileOutputStream(to)) {
            byte[] buffer = new byte[64 * 1024];
            int n;
            while ((n = in.read(buffer)) > 0)
                out.write(buffer, 0, n);
            return true;
        } catch (IOException error) {
            Log.w(TAG, "can't copy the log to " + to + ": " + error);
            return false;
        }
    }

    private static File newFile(File dir) {
        String stamp = new SimpleDateFormat("yyyyMMdd-HHmmss-SSS", Locale.US).format(new Date());
        File file = new File(dir, process + "-" + stamp + ".log");
        prune(dir);
        return file;
    }

    // Keeps the newest files of this process.
    private static void prune(File dir) {
        File[] files = dir.listFiles((d, name) -> name.startsWith(process + "-") && name.endsWith(".log"));
        if (files == null || files.length <= MAX_FILES_PER_PROCESS)
            return;
        Arrays.sort(files);
        for (int i = 0; i < files.length - MAX_FILES_PER_PROCESS; ++i)
            files[i].delete();
    }

    private static String format(int priority, String tag, String message) {
        String time;
        synchronized (lineTime) {
            time = lineTime.format(new Date());
        }
        String thread = Thread.currentThread().getName().replace(' ', '_');
        return String.format(Locale.US, "%s [%9.3f] %s/%s %c %s: %s\n", time, SystemClock.elapsedRealtime() / 1000.0,
            process, thread, level(priority), tag, message.replace("\n", "\n    "));
    }

    private static char level(int priority) {
        switch (priority) {
            case Log.VERBOSE:
                return 'V';
            case Log.DEBUG:
                return 'D';
            case Log.WARN:
                return 'W';
            case Log.ERROR:
                return 'E';
            case Log.ASSERT:
                return 'F';
            default:
                return 'I';
        }
    }

    private static String stackTrace(Throwable error) {
        StringWriter text = new StringWriter();
        error.printStackTrace(new PrintWriter(text));
        return text.toString();
    }

    private static String processName() {
        if (Build.VERSION.SDK_INT >= 28)
            return Application.getProcessName();
        try (FileInputStream in = new FileInputStream("/proc/self/cmdline")) {
            byte[] buffer = new byte[256];
            int n = in.read(buffer);
            int end = 0;
            while (end < n && buffer[end] != 0)
                ++end;
            return new String(buffer, 0, end, StandardCharsets.UTF_8);
        } catch (IOException error) {
            return "?";
        }
    }

    private static File runningMarker() {
        return new File(context.getFilesDir(), process + ".running");
    }

    private static void writeHeader() {
        DisplayMetrics metrics = context.getResources().getDisplayMetrics();
        String version = "?";
        try {
            version = context.getPackageManager().getPackageInfo(context.getPackageName(), 0).versionName;
        } catch (PackageManager.NameNotFoundException ignored) {
        }
        i(TAG, "=== session start: Cloudmus mobile " + version + ", process " + processName() + " pid "
                + Process.myPid() + "\n" + deviceInfo() + "\nScreen " + metrics.widthPixels + "x"
                + metrics.heightPixels + " px, density " + metrics.density + " (" + metrics.densityDpi
                + " dpi), xdpi " + metrics.xdpi + ", ydpi " + metrics.ydpi);
    }

    public static String deviceInfo() {
        return Build.MANUFACTURER + " " + Build.MODEL + " (" + Build.DEVICE + "), Android " + Build.VERSION.RELEASE
            + " / API " + Build.VERSION.SDK_INT + "\nFirmware " + Build.DISPLAY + "\nFingerprint " + Build.FINGERPRINT
            + "\nABIs " + Arrays.toString(Build.SUPPORTED_ABIS);
    }

    // A marker left by the last run of this process means it didn't end on
    // its own: killed by the system, or crashed.
    private static void checkPreviousExit() {
        File marker = runningMarker();
        if (marker.exists()) {
            w(TAG, "the previous " + process + " session ended abruptly (last seen "
                    + new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(new Date(marker.lastModified()))
                    + ")");
        }
        try {
            marker.createNewFile();
            marker.setLastModified(System.currentTimeMillis());
        } catch (IOException ignored) {
        }
        if (Build.VERSION.SDK_INT >= 30) {
            ActivityManager manager = context.getSystemService(ActivityManager.class);
            List<ApplicationExitInfo> exits = manager.getHistoricalProcessExitReasons(null, 0, 4);
            for (ApplicationExitInfo exit : exits)
                i(TAG, "earlier exit: " + exit);
        }
    }
}
