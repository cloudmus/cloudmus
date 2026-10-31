package dev.cloudmus.mobile;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Intent;
import android.os.Bundle;

import androidx.annotation.Nullable;
import androidx.media3.common.MediaItem;
import androidx.media3.common.MediaMetadata;
import androidx.media3.common.util.UnstableApi;
import androidx.media3.session.CommandButton;
import androidx.media3.session.DefaultMediaNotificationProvider;
import androidx.media3.session.LibraryResult;
import androidx.media3.session.MediaLibraryService;
import androidx.media3.session.MediaSession;
import androidx.media3.session.SessionCommand;
import androidx.media3.session.SessionCommands;
import androidx.media3.session.SessionResult;

import com.google.common.collect.ImmutableList;
import com.google.common.util.concurrent.Futures;
import com.google.common.util.concurrent.ListenableFuture;

import java.util.ArrayList;
import java.util.List;

// The music player as the system sees it: a media session (the car's media
// widget, the steering-wheel buttons, Android Auto, the notification) in a
// foreground service in the :playback process, over the C++ core. It lives
// without the UI — Android may unload that at will — and starts without Qt's
// UI libraries; the core is the only native code it brings.
@UnstableApi
public final class PlaybackService extends MediaLibraryService {
    private static final String TAG = "PlaybackService";
    public static final String ACTION_START = "dev.cloudmus.mobile.START";
    public static final String ACTION_AUTOPLAY = "dev.cloudmus.mobile.AUTOPLAY";
    private static final String ROOT_ID = "root";
    private static final String DEV_TRACKS_ID = "dev-tracks";
    private static final String CHANNEL_ID = "playback";
    private static final int NOTIFICATION_ID = DefaultMediaNotificationProvider.DEFAULT_NOTIFICATION_ID;
    // The notification's (and the app's) "stop it all": playback stops and
    // the service, with its process, goes.
    static final String COMMAND_CLOSE = "dev.cloudmus.mobile.CLOSE";
    private static final SessionCommand CLOSE = new SessionCommand(COMMAND_CLOSE, Bundle.EMPTY);

    private MediaLibrarySession session;
    private CorePlayer player;
    private List<Tracks.Entry> tracks;

    @Override
    public void onCreate() {
        CmLog.mark("service onCreate");
        super.onCreate();
        CoreBridge.start(this);
        tracks = Tracks.load(this);
        player = new CorePlayer(this, getMainLooper(), tracks);
        CoreBridge.setListener(player);
        CoreBridge.setStopHandler(() -> new android.os.Handler(getMainLooper()).post(() -> close("stopped in the app")));
        Intent open = new Intent(this, MainActivity.class);
        CommandButton close = new CommandButton.Builder()
            .setDisplayName(getString(R.string.close))
            .setIconResId(R.drawable.ic_close)
            .setSessionCommand(CLOSE)
            .build();
        session = new MediaLibrarySession.Builder(this, player, new Callback())
            .setSessionActivity(PendingIntent.getActivity(this, 0, open,
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT))
            .setCustomLayout(ImmutableList.of(close))
            .build();
        DefaultMediaNotificationProvider notifications = new DefaultMediaNotificationProvider.Builder(this)
            .setChannelId(CHANNEL_ID)
            .setChannelName(R.string.playback_channel)
            .build();
        // A white silhouette: Android 5+ shows the small icon as a mask, the
        // app's colored icon came out as a blank square or not at all.
        notifications.setSmallIcon(R.drawable.ic_stat_cloudmus);
        setMediaNotificationProvider(notifications);
        // Media3 posts the notification (and goes foreground) only for the
        // sessions it knows of — the ones a controller connected to, or
        // added here. Our UI reaches the core over Qt Remote Objects, not a
        // MediaController, so without this there was no notification at all.
        addSession(session);
        CmLog.mark("service ready, " + tracks.size() + " tracks");
        // Off the main thread: startup time is what's measured in the car.
        new Thread(() -> SystemProbe.logAll(this), "system-probe").start();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String action = intent == null ? null : intent.getAction();
        CmLog.mark("service onStartCommand " + action);
        CmLog.i(TAG, "onStartCommand: " + intent + ", flags " + flags);
        if (ACTION_AUTOPLAY.equals(action)) {
            // Started with startForegroundService(): must be foreground within
            // seconds, whether or not playback comes up in time.
            startForeground(NOTIFICATION_ID, placeholderNotification());
            CoreBridge.command(CoreBridge.CMD_PLAY, 0);
        }
        return super.onStartCommand(intent, flags, startId);
    }

    private Notification placeholderNotification() {
        NotificationManager manager = getSystemService(NotificationManager.class);
        manager.createNotificationChannel(new NotificationChannel(CHANNEL_ID, "Playback", NotificationManager.IMPORTANCE_LOW));
        return new Notification.Builder(this, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_stat_cloudmus)
            .setContentTitle("Cloudmus")
            .setOngoing(true)
            .build();
    }

    @Nullable
    @Override
    public MediaLibrarySession onGetSession(MediaSession.ControllerInfo controllerInfo) {
        CmLog.i(TAG, "session requested by " + controllerInfo.getPackageName());
        return session;
    }

    // Pauses, lets go of the audio focus and ends the service — and with it
    // the process (onDestroy()).
    void close(String why) {
        CmLog.i(TAG, "closing: " + why);
        player.pause();
        stopSelf();
    }

    @Override
    public void onUpdateNotification(MediaSession session, boolean startInForegroundRequired) {
        CmLog.i(TAG, "notification update, foreground " + startInForegroundRequired + ", playing "
                + session.getPlayer().getPlayWhenReady() + ", items " + session.getPlayer().getMediaItemCount());
        super.onUpdateNotification(session, startInForegroundRequired);
    }

    @Override
    public void onTaskRemoved(Intent rootIntent) {
        // The UI was swiped away; the music goes on. Without playback there
        // is nothing to keep the service for.
        CmLog.i(TAG, "task removed, playing: " + player.getPlayWhenReady());
        if (!player.getPlayWhenReady())
            stopSelf();
    }

    @Override
    public void onDestroy() {
        CmLog.i(TAG, "service onDestroy");
        CoreBridge.setListener(null);
        CoreBridge.setStopHandler(null);
        if (session != null) {
            session.release();
            session = null;
        }
        player.release();
        CmLog.cleanExit();
        super.onDestroy();
        // The core and its threads end with the process; the service is
        // restarted by the next media button or start.
        android.os.Process.killProcess(android.os.Process.myPid());
    }

    private final class Callback implements MediaLibrarySession.Callback {
        @Override
        public MediaSession.ConnectionResult onConnect(MediaSession session, MediaSession.ControllerInfo controller) {
            CmLog.i(TAG, "controller connected: " + controller.getPackageName() + " (uid " + controller.getUid() + ")");
            MediaSession.ConnectionResult result = MediaLibrarySession.Callback.super.onConnect(session, controller);
            // Everyone may close: the notification, the car's media widget.
            SessionCommands commands = result.availableSessionCommands.buildUpon().add(CLOSE).build();
            return MediaSession.ConnectionResult.accept(commands, result.availablePlayerCommands);
        }

        @Override
        public ListenableFuture<SessionResult> onCustomCommand(MediaSession session,
            MediaSession.ControllerInfo controller, SessionCommand command, Bundle args) {
            if (COMMAND_CLOSE.equals(command.customAction)) {
                close("asked by " + controller.getPackageName());
                return Futures.immediateFuture(new SessionResult(SessionResult.RESULT_SUCCESS));
            }
            return MediaLibrarySession.Callback.super.onCustomCommand(session, controller, command, args);
        }

        @Override
        public void onDisconnected(MediaSession session, MediaSession.ControllerInfo controller) {
            CmLog.i(TAG, "controller disconnected: " + controller.getPackageName());
        }

        @Override
        public boolean onMediaButtonEvent(MediaSession session, MediaSession.ControllerInfo controller, Intent intent) {
            CmLog.i(TAG, "media button from " + controller.getPackageName() + ": " + intent.getParcelableExtra(Intent.EXTRA_KEY_EVENT));
            return MediaLibrarySession.Callback.super.onMediaButtonEvent(session, controller, intent);
        }

        @Override
        public ListenableFuture<LibraryResult<MediaItem>> onGetLibraryRoot(MediaLibrarySession session,
            MediaSession.ControllerInfo browser, @Nullable LibraryParams params) {
            CmLog.i(TAG, "library root for " + browser.getPackageName());
            return Futures.immediateFuture(LibraryResult.ofItem(folder(ROOT_ID, "Cloudmus"), params));
        }

        @Override
        public ListenableFuture<LibraryResult<MediaItem>> onGetItem(MediaLibrarySession session,
            MediaSession.ControllerInfo browser, String mediaId) {
            if (ROOT_ID.equals(mediaId))
                return Futures.immediateFuture(LibraryResult.ofItem(folder(ROOT_ID, "Cloudmus"), null));
            if (DEV_TRACKS_ID.equals(mediaId))
                return Futures.immediateFuture(LibraryResult.ofItem(folder(DEV_TRACKS_ID, "Dev tracks"), null));
            for (Tracks.Entry entry : tracks) {
                if (entry.id.equals(mediaId))
                    return Futures.immediateFuture(LibraryResult.ofItem(entry.item, null));
            }
            return Futures.immediateFuture(LibraryResult.ofError(LibraryResult.RESULT_ERROR_BAD_VALUE));
        }

        @Override
        public ListenableFuture<LibraryResult<ImmutableList<MediaItem>>> onGetChildren(MediaLibrarySession session,
            MediaSession.ControllerInfo browser, String parentId, int page, int pageSize, @Nullable LibraryParams params) {
            CmLog.i(TAG, "children of " + parentId + " for " + browser.getPackageName());
            if (ROOT_ID.equals(parentId))
                return Futures.immediateFuture(LibraryResult.ofItemList(ImmutableList.of(folder(DEV_TRACKS_ID, "Dev tracks")), params));
            if (DEV_TRACKS_ID.equals(parentId)) {
                List<MediaItem> items = new ArrayList<>();
                for (Tracks.Entry entry : tracks)
                    items.add(entry.item);
                return Futures.immediateFuture(LibraryResult.ofItemList(ImmutableList.copyOf(items), params));
            }
            return Futures.immediateFuture(LibraryResult.ofItemList(ImmutableList.of(), params));
        }

        // "Play" with nothing playing — the steering wheel after the car
        // starts, a Bluetooth headset: continue where it was.
        @Override
        public ListenableFuture<MediaSession.MediaItemsWithStartPosition> onPlaybackResumption(
            MediaSession session, MediaSession.ControllerInfo controller) {
            CmLog.i(TAG, "playback resumption requested by " + controller.getPackageName());
            List<MediaItem> items = new ArrayList<>();
            for (Tracks.Entry entry : tracks)
                items.add(entry.item);
            return Futures.immediateFuture(new MediaSession.MediaItemsWithStartPosition(items, 0, 0));
        }

        private MediaItem folder(String id, String title) {
            return new MediaItem.Builder()
                .setMediaId(id)
                .setMediaMetadata(new MediaMetadata.Builder()
                    .setTitle(title)
                    .setIsBrowsable(true)
                    .setIsPlayable(false)
                    .setMediaType(MediaMetadata.MEDIA_TYPE_FOLDER_MIXED)
                    .build())
                .build();
        }
    }
}
