package app.cloudmus.mobile;

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

    // The theme in the views, for the system's part of it.
    public static void setAppearance(Context context, String scheme, String dark, String barColor, String backColor) {
        if (!(context instanceof android.app.Activity))
            return;
        android.app.Activity activity = (android.app.Activity) context;
        activity.runOnUiThread(() -> Appearance.apply(activity, scheme, !dark.isEmpty(), barColor, backColor));
    }

    // A page's own picture is under the status bar: light icons on it.
    public static void setStatusBarOverAura(Context context, String over) {
        if (!(context instanceof android.app.Activity))
            return;
        android.app.Activity activity = (android.app.Activity) context;
        activity.runOnUiThread(() -> Appearance.setOverAura(activity, !over.isEmpty()));
    }

    // "<current> <max>": the music stream's volume, in the system's own steps.
    public static String volume(Context context) {
        android.media.AudioManager audio = context.getSystemService(android.media.AudioManager.class);
        if (audio == null)
            return "";
        int stream = android.media.AudioManager.STREAM_MUSIC;
        return audio.getStreamVolume(stream) + " " + audio.getStreamMaxVolume(stream);
    }

    public static void setVolume(Context context, int index) {
        android.media.AudioManager audio = context.getSystemService(android.media.AudioManager.class);
        if (audio != null)
            audio.setStreamVolume(android.media.AudioManager.STREAM_MUSIC, index, 0);
    }

    // "<top> <left> <right> <bottom>" in pixels: what the system's bars cover.
    public static String windowInsets(Context context) {
        return Appearance.insets();
    }

    private static native void nativeInsetsChanged();

    static void insetsChanged() {
        try {
            nativeInsetsChanged();
        } catch (UnsatisfiedLinkError notLoadedYet) {
            // Qt's still loading: Platform asks for them when it comes up.
        }
    }

    // "<x> <y> <width> <height>" in pixels, in the window: where Android's
    // start screen has its logo; empty without one (StartScreen).
    public static String splashIcon(Context context) {
        return StartScreen.iconRect();
    }

    // StartSplash.qml has drawn the same picture: Android's start screen goes.
    public static void hideSplash(Context context) {
        StartScreen.release();
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
    public static String probeMusicStorage(Context context) {
        context.startService(new Intent(context, PlaybackService.class).setAction(PlaybackService.ACTION_MUSIC_PROBE));
        return "yes";
    }

    // Another app's tracks to delete: the system asks the user.
    public static void requestMediaDelete(Context context, String[] uris) {
        if (context instanceof MainActivity)
            ((MainActivity) context).requestMediaDelete(uris);
    }

    // A source asked for all the device's music: the system's question.
    public static void requestMusicPermission(Context context) {
        if (context instanceof MainActivity)
            ((MainActivity) context).requestMusicPermission();
    }

    // A source's path setting from the system's picker: a folder unless
    // `directory` is empty. The answer comes back in PathPicker.
    public static void pickPath(Context context, String directory) {
        PathPicker.pick(context, !directory.isEmpty());
    }

    public static void grantMusicStorage(Context context) {
        if (context instanceof MainActivity) {
            MainActivity activity = (MainActivity) context;
            activity.runOnUiThread(() -> {
                if (GwmAppSettings.present(activity) && GwmAppSettings.openAppPermissions(activity))
                    return;
                try {
                    activity.startActivity(new Intent(android.provider.Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                        android.net.Uri.fromParts("package", context.getPackageName(), null)));
                } catch (android.content.ActivityNotFoundException error) {
                    CmLog.w("UiBridge", "App permissions screen is unavailable");
                }
            });
        }
    }

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

    // Audio opened from another app (MainActivity): the screen goes to its
    // track. Kept until Qt takes it, as the first one comes before Qt is up.
    private static boolean audioOpened;

    private static native void nativeAudioOpened();

    static void audioOpened() {
        synchronized (UiBridge.class) {
            audioOpened = true;
        }
        try {
            nativeAudioOpened();
        } catch (UnsatisfiedLinkError notLoadedYet) {
            // Platform takes it when it comes up.
        }
    }

    public static synchronized String takeAudioOpened(Context context) {
        boolean opened = audioOpened;
        audioOpened = false;
        return opened ? "yes" : "";
    }

    // The downloads notification tapped (MainActivity): the Downloads page.
    // Kept until Qt takes it, as for audioOpened().
    private static boolean downloadsOpened;

    private static native void nativeDownloadsOpened();

    static void downloadsOpened() {
        synchronized (UiBridge.class) {
            downloadsOpened = true;
        }
        try {
            nativeDownloadsOpened();
        } catch (UnsatisfiedLinkError notLoadedYet) {
            // Platform takes it when it comes up.
        }
    }

    public static synchronized String takeDownloadsOpened(Context context) {
        boolean opened = downloadsOpened;
        downloadsOpened = false;
        return opened ? "yes" : "";
    }

    public static void requestNotificationPermission(Context context) {
        if (context instanceof MainActivity)
            ((MainActivity) context).requestNotificationPermission();
    }

    public static String exportProgress(Context context) {
        return SystemProbe.exportProgress();
    }
}
