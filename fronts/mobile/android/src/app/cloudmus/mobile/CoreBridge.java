package app.cloudmus.mobile;

import android.content.Context;
import android.content.SharedPreferences;

import com.google.common.util.concurrent.ListenableFuture;
import com.google.common.util.concurrent.SettableFuture;

import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;

// The Java side of the core that runs, as C++, in this (:playback) process:
// libcloudmus-core.so, with Qt Core and libmpv. It is loaded and started
// here, driven by commands, and reports its state back through onState().
public final class CoreBridge {
    private static final String TAG = "CoreBridge";
    public static final String PREFS = "core";
    public static final String PREF_AUTOPLAY = "autoplay";
    public static final String PREF_RESUME_AFTER_LOSS = "resumeAfterLossSec";
    // Keep in step with the core's Settings.
    public static final int DEFAULT_RESUME_AFTER_LOSS_SEC = 60;

    public static final int CMD_PLAY = 1;
    public static final int CMD_PAUSE = 2;
    public static final int CMD_NEXT = 3;
    public static final int CMD_PREVIOUS = 4;
    public static final int CMD_SEEK = 5;
    public static final int CMD_LIKE = 6;
    public static final int CMD_DISLIKE = 7;

    public interface Listener {
        // `id` empty: no track. `coverFile` empty: no cover at hand yet.
        void onTrack(String id, String title, String artist, String album, long durationMs, String coverFile);

        void onState(boolean playing, boolean loading, long positionMs, long durationMs);

        void onFeedback(boolean likeSupported, boolean liked, boolean dislikeSupported, boolean disliked);

        // A window of the queue, from queue index `first`, with the track
        // at `current` in it; `current` -1: no queue.
        void onQueue(int first, int current, boolean hasPrevious, boolean hasNext, List<QueueItem> items);
    }

    // One track of the queue, as the system lists it.
    public static final class QueueItem {
        public final String id; // the source's and the track's, as onTrack() has it
        public final String title;
        public final String artist;
        public final String album;
        public final String coverFile; // empty: none at hand yet
        public final long durationMs;

        QueueItem(String id, String title, String artist, String album, String coverFile, long durationMs) {
            this.id = id;
            this.title = title;
            this.artist = artist;
            this.album = album;
            this.coverFile = coverFile;
            this.durationMs = durationMs;
        }
    }

    private static Listener listener;
    private static Runnable stopHandler;
    private static boolean started;

    private CoreBridge() {
    }

    public static native void nativeStart(String filesDir, String cacheDir, String pythonHome, String libraryDir);

    public static native void nativeCommand(int command, long argument);

    public static native void nativeStop();

    private static native void nativeBrowse(int requestId, String id, boolean itemItself);

    private static native void nativePlayMedia(String id);

    private static native void nativePlayQueueItem(int index, String id);

    // One entry of what a media browser lists (ViewModel::MediaTree).
    public static final class BrowseItem {
        public final String id;
        public final String title;
        public final String subtitle;
        public final String coverFile; // empty: none
        public final boolean browsable;

        BrowseItem(String id, String title, String subtitle, String coverFile, boolean browsable) {
            this.id = id;
            this.title = title;
            this.subtitle = subtitle;
            this.coverFile = coverFile;
            this.browsable = browsable;
        }
    }

    private static final AtomicInteger nextBrowseRequest = new AtomicInteger();
    private static final Map<Integer, SettableFuture<List<BrowseItem>>> browseRequests = new ConcurrentHashMap<>();
    private static Runnable libraryListener;

    // What's in a folder of the core's tree. Answered once the core is up:
    // a car's Media Center binds to the service and asks at once. `itemItself`:
    // the entry for this id instead, on its own in the list, or none.
    public static ListenableFuture<List<BrowseItem>> browse(String id, boolean itemItself) {
        SettableFuture<List<BrowseItem>> result = SettableFuture.create();
        int request = nextBrowseRequest.incrementAndGet();
        browseRequests.put(request, result);
        nativeBrowse(request, id, itemItself);
        return result;
    }

    public static void playMedia(String id) {
        nativePlayMedia(id);
    }

    // `index` in the queue, where the track `id` was when last reported.
    public static void playQueueItem(int index, String id) {
        if (started)
            nativePlayQueueItem(index, id);
    }

    public static synchronized void setLibraryListener(Runnable listener) {
        libraryListener = listener;
    }

    public static boolean autoplayEnabled(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getBoolean(PREF_AUTOPLAY, true);
    }

    // See ViewModel::Settings::resumeAfterLossSec; 0: off.
    public static int resumeAfterLossSec(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .getInt(PREF_RESUME_AFTER_LOSS, DEFAULT_RESUME_AFTER_LOSS_SEC);
    }

    public static synchronized void setListener(Listener value) {
        listener = value;
    }

    // What to do when a view asks the player to stop for good.
    public static synchronized void setStopHandler(Runnable handler) {
        stopHandler = handler;
    }

    // Loads the core and starts it on a thread of its own. Idempotent. The
    // service can't wait here: Python's first unpacking takes many seconds.
    // Commands that come earlier wait in the core's library.
    public static synchronized void start(Context context) {
        if (started)
            return;
        started = true;
        CmLog.mark("core: loading");
        // Qt Core in a process without Qt's activity: it needs the JavaVM
        // (set by its own JNI_OnLoad, which only runs for a library loaded
        // by name — not one pulled in as a dependency) and a context to ask
        // the system things with (the service, the way Qt's own services do).
        // In this order: Qt Core reads the context while it loads, and without
        // one QStandardPaths comes back empty, so nothing gets saved.
        if (context instanceof android.app.Service)
            org.qtproject.qt.android.QtNative.setService((android.app.Service) context);
        loadQtCore();
        System.loadLibrary("cloudmus-core");
        CmLog.mark("core: library loaded");
        Context application = context.getApplicationContext();
        new Thread(() -> {
            File python = null;
            try {
                python = PythonRuntime.prepare(application);
            } catch (Exception error) {
                CmLog.e(TAG, "no Python, so no sources", error);
            }
            nativeStart(application.getFilesDir().getPath(), application.getCacheDir().getPath(),
                python == null ? "" : python.getPath(), application.getApplicationInfo().nativeLibraryDir);
        }, "core-start").start();
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

    // From the core, on its thread.
    @SuppressWarnings("unused")
    static void onState(boolean playing, boolean loading, long positionMs, long durationMs) {
        Listener current;
        synchronized (CoreBridge.class) {
            current = listener;
        }
        if (current != null)
            current.onState(playing, loading, positionMs, durationMs);
    }

    // From the core, on its thread.
    @SuppressWarnings("unused")
    static void onFeedback(boolean likeSupported, boolean liked, boolean dislikeSupported, boolean disliked) {
        Listener current;
        synchronized (CoreBridge.class) {
            current = listener;
        }
        if (current != null)
            current.onFeedback(likeSupported, liked, dislikeSupported, disliked);
    }

    // From the core, on its thread.
    @SuppressWarnings("unused")
    static void onTrack(String id, String title, String artist, String album, long durationMs, String coverFile) {
        Listener current;
        synchronized (CoreBridge.class) {
            current = listener;
        }
        if (current != null)
            current.onTrack(id, title, artist, album, durationMs, coverFile);
    }

    // From the core, on its thread: six strings for each item, in QueueItem's
    // order.
    @SuppressWarnings("unused")
    static void onQueue(int first, int current, boolean hasPrevious, boolean hasNext, String[] flat) {
        Listener target;
        synchronized (CoreBridge.class) {
            target = listener;
        }
        if (target == null)
            return;
        List<QueueItem> items = new ArrayList<>(flat.length / 6);
        for (int i = 0; i + 5 < flat.length; i += 6) {
            long durationMs;
            try {
                durationMs = Long.parseLong(flat[i + 5]);
            } catch (NumberFormatException error) {
                durationMs = 0;
            }
            items.add(new QueueItem(flat[i], flat[i + 1], flat[i + 2], flat[i + 3], flat[i + 4], durationMs));
        }
        target.onQueue(first, current, hasPrevious, hasNext, items);
    }

    // From the core, on its thread: five strings for each item, in BrowseItem's
    // order, a non-empty last one for a folder.
    @SuppressWarnings("unused")
    static void onBrowseResult(int request, String[] flat) {
        SettableFuture<List<BrowseItem>> result = browseRequests.remove(request);
        if (result == null)
            return;
        List<BrowseItem> items = new ArrayList<>(flat.length / 5);
        for (int i = 0; i + 4 < flat.length; i += 5)
            items.add(new BrowseItem(flat[i], flat[i + 1], flat[i + 2], flat[i + 3], !flat[i + 4].isEmpty()));
        result.set(items);
    }

    // From the core, on its thread.
    @SuppressWarnings("unused")
    static void onLibraryChanged() {
        Runnable listener;
        synchronized (CoreBridge.class) {
            listener = libraryListener;
        }
        if (listener != null)
            listener.run();
    }

    // From the core, when the user changes autoplay in Settings: kept
    // where the boot receiver can read it without starting the core.
    @SuppressWarnings("unused")
    static void onAutoplayChanged(boolean enabled) {
        CmLog.i(TAG, "autoplay " + (enabled ? "on" : "off"));
        Context context = CmLog.context();
        if (context != null)
            context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putBoolean(PREF_AUTOPLAY, enabled).apply();
    }

    // From the core, when the user changes it in Settings: CorePlayer
    // reads it when the focus is lost.
    @SuppressWarnings("unused")
    static void onResumeAfterLossChanged(int seconds) {
        CmLog.i(TAG, "resume after focus loss: " + (seconds > 0 ? "within " + seconds + " s" : "off"));
        Context context = CmLog.context();
        if (context != null)
            context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putInt(PREF_RESUME_AFTER_LOSS, seconds).apply();
    }

    // From the core: a view stopped the player (NowPlaying::stop()).
    @SuppressWarnings("unused")
    static void onStopped() {
        Runnable handler;
        synchronized (CoreBridge.class) {
            handler = stopHandler;
        }
        if (handler != null)
            handler.run();
    }

    @SuppressWarnings("unused")
    static void onCoreReady() {
        CmLog.mark("core: ready");
    }
}
