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
        context.startService(intent);
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

    // Blocks for long: from a worker thread, the UI polls exportProgress().
    public static String exportSystemApps(Context context) {
        return SystemProbe.exportApks(context);
    }

    public static String exportProgress(Context context) {
        return SystemProbe.exportProgress();
    }
}
