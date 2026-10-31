package dev.cloudmus.mobile;

import android.content.Context;
import android.content.SharedPreferences;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

// The Java side of the core that runs, as C++, in this (:playback) process:
// libcloudmus-core.so, with Qt Core and libmpv. It is loaded and started
// here, driven by commands, and reports its state back through onState().
public final class CoreBridge {
    private static final String TAG = "CoreBridge";
    public static final String PREFS = "core";
    public static final String PREF_AUTOPLAY = "autoplay";

    public static final int CMD_PLAY = 1;
    public static final int CMD_PAUSE = 2;
    public static final int CMD_NEXT = 3;
    public static final int CMD_PREVIOUS = 4;
    public static final int CMD_SEEK = 5;
    public static final int CMD_PLAY_INDEX = 6;

    public interface Listener {
        void onState(int index, boolean playing, boolean loading, long positionMs, long durationMs);
    }

    private static Listener listener;
    private static boolean started;

    private CoreBridge() {
    }

    public static native void nativeStart(String filesDir, String mediaDir);

    public static native void nativeCommand(int command, long argument);

    public static native void nativeStop();

    public static File mediaDir(Context context) {
        return new File(context.getFilesDir(), "media");
    }

    public static boolean autoplayEnabled(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getBoolean(PREF_AUTOPLAY, true);
    }

    public static synchronized void setListener(Listener value) {
        listener = value;
    }

    // Loads the core and starts it on a thread of its own. Idempotent.
    public static synchronized void start(Context context) {
        if (started)
            return;
        started = true;
        CmLog.mark("core: loading");
        copyMedia(context);
        // Qt Core in a process without Qt's activity: it needs the JavaVM
        // (set by its own JNI_OnLoad, which only runs for a library loaded
        // by name — not one pulled in as a dependency) and a context to ask
        // the system things with (the service, the way Qt's own services do).
        loadQtCore();
        if (context instanceof android.app.Service)
            org.qtproject.qt.android.QtNative.setService((android.app.Service) context);
        System.loadLibrary("cloudmus-core");
        CmLog.mark("core: library loaded");
        nativeStart(context.getFilesDir().getPath(), mediaDir(context).getPath());
    }

    private static void loadQtCore() {
        for (String abi : android.os.Build.SUPPORTED_ABIS) {
            try {
                System.loadLibrary("Qt6Core_" + abi);
                return;
            } catch (UnsatisfiedLinkError ignored) {
            }
        }
        CmLog.w(TAG, "Qt6Core wasn't found for the ABIs " + java.util.Arrays.toString(android.os.Build.SUPPORTED_ABIS));
    }

    public static void command(int command, long argument) {
        if (started)
            nativeCommand(command, argument);
        else
            CmLog.w(TAG, "command " + command + " before the core started");
    }

    // Tracks and covers travel in the APK's assets; mpv wants real files.
    private static void copyMedia(Context context) {
        File dir = mediaDir(context);
        dir.mkdirs();
        long begin = System.nanoTime();
        int copied = 0;
        try {
            String[] names = context.getAssets().list("media");
            if (names == null || names.length == 0) {
                CmLog.w(TAG, "no media in the APK");
                return;
            }
            // What's there is from this install if it's newer than it.
            long installedAt = context.getPackageManager().getPackageInfo(context.getPackageName(), 0).lastUpdateTime;
            for (String name : names) {
                File target = new File(dir, name);
                if (target.exists() && target.lastModified() >= installedAt)
                    continue;
                try (InputStream in = context.getAssets().open("media/" + name);
                     OutputStream out = new FileOutputStream(target)) {
                    byte[] buffer = new byte[256 * 1024];
                    int n;
                    while ((n = in.read(buffer)) > 0)
                        out.write(buffer, 0, n);
                }
                ++copied;
            }
        } catch (IOException | android.content.pm.PackageManager.NameNotFoundException error) {
            CmLog.e(TAG, "copying media failed", error);
        }
        CmLog.i(TAG, "media: " + copied + " files copied in " + (System.nanoTime() - begin) / 1000000 + " ms");
    }

    // From the core, on its thread.
    @SuppressWarnings("unused")
    static void onState(int index, boolean playing, boolean loading, long positionMs, long durationMs) {
        Listener current;
        synchronized (CoreBridge.class) {
            current = listener;
        }
        if (current != null)
            current.onState(index, playing, loading, positionMs, durationMs);
    }

    // From the core, when the user changes autoplay in Diagnostics: kept
    // where the boot receiver can read it without starting the core.
    @SuppressWarnings("unused")
    static void onAutoplayChanged(boolean enabled) {
        CmLog.i(TAG, "autoplay " + (enabled ? "on" : "off"));
        Context context = CmLog.context();
        if (context != null)
            context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putBoolean(PREF_AUTOPLAY, enabled).apply();
    }

    @SuppressWarnings("unused")
    static void onCoreReady() {
        CmLog.mark("core: ready");
    }
}
