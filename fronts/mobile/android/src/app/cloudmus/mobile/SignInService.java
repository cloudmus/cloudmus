package app.cloudmus.mobile;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.IBinder;

// Keeps the :playback process, where the core and the sources run, while a
// sign-in waits for its code to be confirmed in a browser. PlaybackService is
// foreground only while it plays, and before a sign-in nothing does: opening
// the browser left the process in the background, and a phone short of memory
// (Tecno's HiOS) unloaded it, the code with it. A foreground service of its
// own, with its notification, for as long as the code is waited for
// (MobileCore::SignInNotice); the source stops it by signing in, failing, or
// its code expiring.
public final class SignInService extends Service {
    private static final String TAG = "SignInService";
    private static final String CHANNEL_ID = "sign-in";
    private static final int NOTIFICATION_ID = 3;
    private static final String EXTRA_TITLE = "title";
    private static final String EXTRA_TEXT = "text";
    private static final String EXTRA_CHANNEL = "channel";

    private static String channelName = "";

    // From the core's thread: an empty title stops it.
    static void update(Context context, String title, String text, String channel) {
        Intent intent = new Intent(context, SignInService.class);
        if (title.isEmpty()) {
            context.stopService(intent);
            return;
        }
        intent.putExtra(EXTRA_TITLE, title).putExtra(EXTRA_TEXT, text).putExtra(EXTRA_CHANNEL, channel);
        try {
            context.startForegroundService(intent);
        } catch (RuntimeException error) {
            // Android 12+ doesn't let it start with the app in the background
            // (ForegroundServiceStartNotAllowedException). A sign-in is asked
            // for with the app open, so this is a code arriving late: it's
            // still kept by the backend for the next start.
            CmLog.w(TAG, "can't keep the process: " + error.getMessage());
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent == null) {
            stopSelf();
            return START_NOT_STICKY;
        }
        String channel = intent.getStringExtra(EXTRA_CHANNEL);
        NotificationManager manager = getSystemService(NotificationManager.class);
        // Named in the app's language, and again when that changes.
        if (channel != null && !channel.equals(channelName)) {
            channelName = channel;
            manager.createNotificationChannel(
                new NotificationChannel(CHANNEL_ID, channel, NotificationManager.IMPORTANCE_LOW));
        }
        Intent open = new Intent(this, MainActivity.class)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        Notification notification = new Notification.Builder(this, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_stat_cloudmus)
            .setContentTitle(intent.getStringExtra(EXTRA_TITLE))
            .setContentText(intent.getStringExtra(EXTRA_TEXT))
            .setContentIntent(PendingIntent.getActivity(this, 2, open,
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT))
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setShowWhen(false)
            .build();
        try {
            // dataSync: waiting on the network for the token, the closest
            // of the types; mediaPlayback would claim music that isn't there.
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q)
                startForeground(NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
            else
                startForeground(NOTIFICATION_ID, notification);
            CmLog.i(TAG, "keeping the process for a sign-in");
        } catch (RuntimeException error) {
            CmLog.w(TAG, "can't go foreground: " + error.getMessage());
            stopSelf();
        }
        return START_NOT_STICKY;
    }

    @Override
    public void onDestroy() {
        CmLog.i(TAG, "the sign-in is over");
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}
