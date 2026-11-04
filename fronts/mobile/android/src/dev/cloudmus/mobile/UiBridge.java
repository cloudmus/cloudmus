package dev.cloudmus.mobile;

import android.content.Context;
import android.content.Intent;

import java.io.File;

// What the Qt UI process asks of Android (called from C++, Platform.cpp).
public final class UiBridge {
    private UiBridge() {
    }

    // Brings the playback service, and the core in it, up.
    public static void startService(Context context) {
        CmLog.i("UiBridge", "starting the playback service");
        Intent intent = new Intent(context, PlaybackService.class).setAction(PlaybackService.ACTION_START);
        try {
            context.startService(intent);
        } catch (IllegalStateException error) {
            // Android allows it only with the app in front: MainActivity asks
            // again once it is (the screen was off, or it isn't resumed yet).
            CmLog.w("UiBridge", "the playback service can't start yet: " + error.getMessage());
        }
    }

    // Non-empty with a screen reader (TalkBack and the like) on.
    public static String screenReaderOn(Context context) {
        android.view.accessibility.AccessibilityManager manager
            = context.getSystemService(android.view.accessibility.AccessibilityManager.class);
        return manager != null && manager.isTouchExplorationEnabled() ? "yes" : "";
    }

    public static String publicLogDirectory(Context context) {
        File dir = CmLog.publicDirectory();
        return dir == null ? "" : dir.getPath();
    }

    public static String deviceInfo(Context context) {
        return CmLog.deviceInfo();
    }

    public static String exportLogs(Context context) {
        return CmLog.exportZip();
    }

    // What the head unit's media system looks like, into the log: a few
    // seconds of package queries, so off the calling thread. Run by hand
    // from Developer tools; at every start it filled the log.
    public static String logSystemProbe(Context context) {
        new Thread(() -> SystemProbe.logAll(context), "system-probe").start();
        return "Written to the log";
    }

    // Blocks for long: from a worker thread, the UI polls exportProgress().
    public static String exportSystemApps(Context context) {
        return SystemProbe.exportApks(context);
    }

    // Where Python is, for starting it: "<PYTHONHOME>\n<native library dir>",
    // or empty when it can't be had (an APK without it). Blocks on the first
    // run: from a worker thread.
    public static String preparePython(Context context) {
        try {
            return PythonRuntime.prepare(context).getPath() + "\n" + context.getApplicationInfo().nativeLibraryDir;
        } catch (Exception error) {
            CmLog.e("UiBridge", "no Python", error);
            return "";
        }
    }

    // The Python probe asked for as the app was started (`am start ... --ez
    // pythonProbe true`), once.
    public static boolean pythonProbeRequested;

    public static String takePythonProbeRequest(Context context) {
        boolean requested = pythonProbeRequested;
        pythonProbeRequested = false;
        return requested ? "yes" : "";
    }

    public static String exportProgress(Context context) {
        return SystemProbe.exportProgress();
    }
}
