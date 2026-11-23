package app.cloudmus.mobile;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

// The notification of what's being saved, in the :playback process where the
// core saves it: the track or playlist under way with a bar for all of it, a
// Cancel button, and a tap that opens the Downloads page. Once it's all over,
// how the last one ended, to be swiped away. A head unit that shows no
// notifications has the same on the app's main screen.
final class DownloadNotice {
    private static final String TAG = "DownloadNotice";
    private static final String CHANNEL_ID = "downloads";
    private static final int NOTIFICATION_ID = 2;
    static final String ACTION_CANCEL = "app.cloudmus.mobile.CANCEL_DOWNLOADS";
    // MainActivity: open the Downloads page.
    static final String EXTRA_OPEN_DOWNLOADS = "openDownloads";

    private static String channelName = "";

    private DownloadNotice() {
    }

    static synchronized void show(Context context, boolean shown, boolean ongoing, boolean failed, String title,
                                  String text, int permille, String cancelLabel, String name) {
        NotificationManager manager = context.getSystemService(NotificationManager.class);
        if (manager == null)
            return;
        if (!shown) {
            manager.cancel(NOTIFICATION_ID);
            return;
        }
        // Named in the app's language, and again when that changes.
        if (!name.equals(channelName)) {
            channelName = name;
            manager.createNotificationChannel(
                new NotificationChannel(CHANNEL_ID, name, NotificationManager.IMPORTANCE_LOW));
        }
        Intent open = new Intent(context, MainActivity.class)
            .putExtra(EXTRA_OPEN_DOWNLOADS, true)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        Notification.Builder builder = new Notification.Builder(context, CHANNEL_ID)
            .setContentTitle(title)
            .setContentText(text)
            .setContentIntent(PendingIntent.getActivity(context, 1, open,
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT))
            .setOnlyAlertOnce(true)
            .setShowWhen(false);
        if (ongoing) {
            Intent cancel = new Intent(context, CancelReceiver.class).setAction(ACTION_CANCEL);
            builder.setSmallIcon(android.R.drawable.stat_sys_download)
                .setOngoing(true)
                .setProgress(1000, Math.max(permille, 0), permille < 0)
                .addAction(new Notification.Action.Builder(null, cancelLabel,
                    PendingIntent.getBroadcast(context, 0, cancel,
                        PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT)).build());
        } else {
            builder.setSmallIcon(failed ? android.R.drawable.stat_notify_error : android.R.drawable.stat_sys_download_done)
                .setAutoCancel(true);
        }
        try {
            manager.notify(NOTIFICATION_ID, builder.build());
        } catch (RuntimeException error) {
            // Not allowed to post (Android 13+ without the permission): the
            // app's own screen still shows it.
            CmLog.w(TAG, "can't post: " + error.getMessage());
        }
    }

    // A core starting anew has nothing being saved: what a killed one left up
    // goes.
    static void clear(Context context) {
        NotificationManager manager = context.getSystemService(NotificationManager.class);
        if (manager != null)
            manager.cancel(NOTIFICATION_ID);
    }

    // The notification's Cancel: everything being saved stops.
    public static final class CancelReceiver extends BroadcastReceiver {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (ACTION_CANCEL.equals(intent.getAction()))
                CoreBridge.command(CoreBridge.CMD_CANCEL_DOWNLOADS, 0);
        }
    }
}
