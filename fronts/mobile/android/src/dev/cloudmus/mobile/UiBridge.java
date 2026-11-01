package dev.cloudmus.mobile;

import android.content.ComponentName;
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

    // GWM head units: the car's media widget lists a fixed set of apps, plus
    // the one last picked in Media Center. Its dispatcher picks us as asked,
    // the same as the user choosing us there, and opens Media Center.
    public static String connectToMediaCenter(Context context) {
        String component = new ComponentName(context, PlaybackService.class).flattenToString();
        Intent intent = new Intent("android.car.intent.action.MEDIA_TEMPLATE")
            .setClassName("com.gwm.app.car.media", "com.android.car.media.MediaDispatcherActivity")
            .putExtra("android.car.intent.extra.MEDIA_COMPONENT", component)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        try {
            context.startActivity(intent);
            CmLog.i("UiBridge", "asked Media Center to connect to " + component);
            return "Media Center opened";
        } catch (RuntimeException error) {
            // No such app on this unit, or it refused.
            CmLog.e("UiBridge", "can't open Media Center", error);
            return "Can't open Media Center: " + error.getMessage();
        }
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
