package app.cloudmus.mobile;

import android.app.Activity;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.InstallSourceInfo;
import android.content.pm.PackageInstaller;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.provider.Settings;

import java.io.FileInputStream;
import java.io.InputStream;
import java.io.OutputStream;

// Installing an update of the app the app downloaded itself (the APK from
// the GitHub release, ViewModel::UpdateScreen): Android's package installer,
// after the user allowed CloudMus to install apps. The installer stops this
// app's processes; the new version is started again by relaunchAfterUpdate()
// from BootReceiver, which gets MY_PACKAGE_REPLACED.
//
// Also who installed the app, for which way it's updated.
public final class UpdateInstaller {
    private static final String TAG = "UpdateInstaller";
    private static final String PREFERENCES = "updates";
    // An update the app started: the new version opens again. When: one the
    // installer never finished doesn't open the app at some later update.
    private static final String KEY_RELAUNCH = "relaunch";
    private static final String KEY_RELAUNCH_AT = "relaunchAt";
    private static final long RELAUNCH_WITHIN_MS = 30 * 60 * 1000;
    // The music played then: it goes on.
    private static final String KEY_RESUME = "resume";
    // The notification's texts, in the app's language: the new version
    // posts it before any of its Qt is up.
    private static final String KEY_CHANNEL = "channel";
    private static final String KEY_TITLE = "title";
    private static final String KEY_TEXT = "text";
    private static final String CHANNEL_ID = "updates";
    private static final int NOTIFICATION_ID = 7401;

    // What Platform::installFinished() is told.
    static final int STATUS_CANCELLED = 0;
    static final int STATUS_FAILED = 1;

    private UpdateInstaller() {
    }

    // The store the app came from: "play", "rustore", or "" for anything
    // else (adb, a browser, another store).
    public static String installer(Context context) {
        String name = null;
        try {
            PackageManager packages = context.getPackageManager();
            if (Build.VERSION.SDK_INT >= 30) {
                InstallSourceInfo source = packages.getInstallSourceInfo(context.getPackageName());
                name = source.getInstallingPackageName();
            } else {
                name = packages.getInstallerPackageName(context.getPackageName());
            }
        } catch (Exception error) {
            CmLog.e(TAG, "who installed the app", error);
        }
        CmLog.i(TAG, "installed by " + name);
        if ("com.android.vending".equals(name))
            return "play";
        if ("ru.vk.store".equals(name))
            return "rustore";
        return "";
    }

    // Whether the user allowed CloudMus to install apps ("Install unknown
    // apps" in Android's settings): "yes" or "".
    public static String canInstall(Context context) {
        return context.getPackageManager().canRequestPackageInstalls() ? "yes" : "";
    }

    // That setting's screen, for CloudMus.
    public static void requestPermission(Context context) {
        Intent intent = new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
            Uri.parse("package:" + context.getPackageName()));
        if (!(context instanceof Activity))
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        try {
            context.startActivity(intent);
        } catch (Exception error) {
            CmLog.e(TAG, "open the install permission", error);
            nativeInstallFinished(STATUS_FAILED, 0, error.toString());
        }
    }

    // Hands `path` to the package installer, off the calling thread; the
    // outcome comes back to StatusReceiver. `resume`: the music is playing.
    // `notice`: the "updated" notification's channel, title and text.
    public static void install(Context context, String path, String resume, String[] notice) {
        Context app = context.getApplicationContext();
        preferences(app).edit()
            .putString(KEY_CHANNEL, notice[0]).putString(KEY_TITLE, notice[1]).putString(KEY_TEXT, notice[2])
            .commit();
        new Thread(() -> {
            try {
                commit(app, path, !resume.isEmpty());
            } catch (Exception error) {
                CmLog.e(TAG, "install " + path, error);
                forgetRelaunch(app);
                nativeInstallFinished(STATUS_FAILED, 0,
                    error.getMessage() != null ? error.getMessage() : error.toString());
            }
        }, "update-install").start();
    }

    private static void commit(Context context, String path, boolean resume) throws Exception {
        PackageInstaller installer = context.getPackageManager().getPackageInstaller();
        PackageInstaller.SessionParams params
            = new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
        params.setAppPackageName(context.getPackageName());
        if (Build.VERSION.SDK_INT >= 31) {
            // Once CloudMus installed itself, Android lets it update itself
            // without asking each time.
            params.setRequireUserAction(PackageInstaller.SessionParams.USER_ACTION_NOT_REQUIRED);
        }
        int id = installer.createSession(params);
        try (PackageInstaller.Session session = installer.openSession(id)) {
            try (InputStream in = new FileInputStream(path);
                 OutputStream out = session.openWrite("CloudMus.apk", 0, new java.io.File(path).length())) {
                byte[] buffer = new byte[256 * 1024];
                int read;
                while ((read = in.read(buffer)) > 0)
                    out.write(buffer, 0, read);
                session.fsync(out);
            }
            preferences(context).edit().putBoolean(KEY_RELAUNCH, true).putLong(KEY_RELAUNCH_AT, System.currentTimeMillis())
                .putBoolean(KEY_RESUME, resume).commit();
            Intent status = new Intent(context, StatusReceiver.class);
            int flags = PendingIntent.FLAG_UPDATE_CURRENT
                | (Build.VERSION.SDK_INT >= 31 ? PendingIntent.FLAG_MUTABLE : 0);
            PendingIntent pending = PendingIntent.getBroadcast(context, id, status, flags);
            CmLog.i(TAG, "installing " + path + " (session " + id + ")");
            session.commit(pending.getIntentSender());
        }
    }

    // The installer's answers: its own question to the user, then how it went.
    public static final class StatusReceiver extends BroadcastReceiver {
        @Override
        public void onReceive(Context context, Intent intent) {
            int status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE);
            String message = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE);
            CmLog.i(TAG, "install status " + status + (message != null ? ": " + message : ""));
            switch (status) {
            case PackageInstaller.STATUS_PENDING_USER_ACTION: {
                Intent confirm = intent.getParcelableExtra(Intent.EXTRA_INTENT);
                if (confirm == null)
                    return;
                confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                try {
                    context.startActivity(confirm);
                } catch (Exception error) {
                    CmLog.e(TAG, "ask to install", error);
                    forgetRelaunch(context);
                    nativeInstallFinished(STATUS_FAILED, 0, error.toString());
                }
                return;
            }
            case PackageInstaller.STATUS_SUCCESS:
                return; // this process is on its way out
            case PackageInstaller.STATUS_FAILURE_ABORTED:
                forgetRelaunch(context);
                nativeInstallFinished(STATUS_CANCELLED, status, "");
                return;
            default:
                // Platform words it by `status`.
                forgetRelaunch(context);
                nativeInstallFinished(STATUS_FAILED, status, message != null ? message : "");
            }
        }
    }

    // BootReceiver, on MY_PACKAGE_REPLACED: the new version, after an update
    // the app itself started, opens again and plays on if it was playing.
    // Android 10+ doesn't let an app open its screen from the background: a
    // notification to tap instead.
    static void relaunchAfterUpdate(Context context) {
        SharedPreferences preferences = preferences(context);
        long age = System.currentTimeMillis() - preferences.getLong(KEY_RELAUNCH_AT, 0);
        if (!preferences.getBoolean(KEY_RELAUNCH, false) || age < 0 || age > RELAUNCH_WITHIN_MS) {
            forgetRelaunch(context);
            return;
        }
        boolean resume = preferences.getBoolean(KEY_RESUME, false);
        String channel = preferences.getString(KEY_CHANNEL, "Updates");
        String title = preferences.getString(KEY_TITLE, "CloudMus is updated");
        String text = preferences.getString(KEY_TEXT, "");
        forgetRelaunch(context);
        CmLog.i(TAG, "updated; opening again" + (resume ? ", the music too" : ""));
        if (resume) {
            Intent start = new Intent(context, PlaybackService.class).setAction(PlaybackService.ACTION_AUTOPLAY);
            try {
                context.startForegroundService(start);
            } catch (Exception error) {
                CmLog.e(TAG, "resume the music", error);
            }
        }
        Intent open = new Intent(context, MainActivity.class)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_RESET_TASK_IF_NEEDED);
        try {
            context.startActivity(open);
        } catch (Exception error) {
            CmLog.e(TAG, "open the app", error);
        }
        if (Build.VERSION.SDK_INT >= 29)
            notifyUpdated(context, open, channel, title, text);
    }

    private static void notifyUpdated(Context context, Intent open, String channel, String title, String text) {
        NotificationManager manager = context.getSystemService(NotificationManager.class);
        if (manager == null)
            return;
        manager.createNotificationChannel(
            new NotificationChannel(CHANNEL_ID, channel, NotificationManager.IMPORTANCE_DEFAULT));
        PendingIntent tap = PendingIntent.getActivity(context, 0, open,
            PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        String version = "";
        try {
            version = context.getPackageManager().getPackageInfo(context.getPackageName(), 0).versionName;
        } catch (Exception ignored) {
        }
        Notification notification = new Notification.Builder(context, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_stat_cloudmus)
            .setContentTitle(title.replace("%1", version))
            .setContentText(text)
            .setContentIntent(tap)
            .setAutoCancel(true)
            .build();
        try {
            manager.notify(NOTIFICATION_ID, notification);
        } catch (SecurityException noPermission) {
            CmLog.i(TAG, "no notification permission: " + noPermission);
        }
    }

    // The app is on screen: the notification has done its job.
    static void clearUpdatedNotice(Context context) {
        NotificationManager manager = context.getSystemService(NotificationManager.class);
        if (manager != null)
            manager.cancel(NOTIFICATION_ID);
    }

    private static void forgetRelaunch(Context context) {
        preferences(context).edit().remove(KEY_RELAUNCH).remove(KEY_RELAUNCH_AT).remove(KEY_RESUME).commit();
    }

    private static SharedPreferences preferences(Context context) {
        return context.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE);
    }

    // `installerStatus`: PackageInstaller's STATUS_*, 0 if it never answered.
    private static native void nativeInstallFinishedImpl(int status, int installerStatus, String message);

    static void nativeInstallFinished(int status, int installerStatus, String message) {
        try {
            nativeInstallFinishedImpl(status, installerStatus, message);
        } catch (UnsatisfiedLinkError noUi) {
            // No UI in this process (it was started for the receiver): the
            // core's page shows Installing until it's opened again.
            CmLog.i(TAG, "install finished with no UI to tell: " + status + " " + message);
        }
    }
}
