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
import com.google.common.util.concurrent.MoreExecutors;
import com.google.common.util.concurrent.SettableFuture;

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
    // Keep in step with ViewModel::MediaTree.
    static final String PLAYLIST_ID_PREFIX = "playlist/";
    private static final long ROOT_WAIT_MS = 10000;
    private static final long ROOT_POLL_MS = 500;
    private static final String CHANNEL_ID = "playback";
    private static final int NOTIFICATION_ID = DefaultMediaNotificationProvider.DEFAULT_NOTIFICATION_ID;
    // The notification's (and the app's) "stop it all": playback stops and
    // the service, with its process, goes.
    static final String COMMAND_CLOSE = "dev.cloudmus.mobile.CLOSE";
    private static final SessionCommand CLOSE = new SessionCommand(COMMAND_CLOSE, Bundle.EMPTY);
    // The car's media widget opens Media Center, not the player's screen, on
    // a tap: a third-party player is shown there. This button, in Media
    // Center's controls, gets to the screen.
    static final String COMMAND_OPEN = "dev.cloudmus.mobile.OPEN";
    private static final SessionCommand OPEN = new SessionCommand(COMMAND_OPEN, Bundle.EMPTY);
    static final String COMMAND_LIKE = "dev.cloudmus.mobile.LIKE";
    private static final SessionCommand LIKE = new SessionCommand(COMMAND_LIKE, Bundle.EMPTY);
    static final String COMMAND_DISLIKE = "dev.cloudmus.mobile.DISLIKE";
    private static final SessionCommand DISLIKE = new SessionCommand(COMMAND_DISLIKE, Bundle.EMPTY);

    private MediaLibrarySession session;
    private CorePlayer player;
    private PendingIntent openScreen;
    private boolean autoplayed;
    // The source folders browsers have opened.
    private final java.util.Set<String> knownFolders = java.util.concurrent.ConcurrentHashMap.newKeySet();

    @Override
    public void onCreate() {
        CmLog.mark("service onCreate");
        super.onCreate();
        CoreBridge.start(this);
        player = new CorePlayer(this, getMainLooper());
        CoreBridge.setListener(player);
        CoreBridge.setStopHandler(() -> new android.os.Handler(getMainLooper()).post(() -> close("stopped in the app")));
        Intent open = new Intent(this, MainActivity.class);
        openScreen = PendingIntent.getActivity(this, 0, open,
            PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        session = new MediaLibrarySession.Builder(this, player, new Callback())
            .setSessionActivity(openScreen)
            .setCustomLayout(buttons())
            .build();
        player.setFeedbackListener(() -> {
            if (session != null)
                session.setCustomLayout(buttons());
        });
        CoreBridge.setLibraryListener(() -> new android.os.Handler(getMainLooper()).post(this::notifyLibraryChanged));
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
        CmLog.mark("service ready");
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
            autoplayed = true;
            CoreBridge.command(CoreBridge.CMD_PLAY, 0);
        } else if (ACTION_START.equals(action) && !autoplayed) {
            // The app opened, for the first time since this process started (by
            // a car's media hub just looking, or a button, it doesn't count): the
            // music goes on if that's the setting. Only then: opening the app again
            // doesn't undo a pause.
            autoplayed = true;
            if (CoreBridge.autoplayEnabled(this)) {
                CmLog.i(TAG, "autoplay: the app opened");
                CoreBridge.command(CoreBridge.CMD_PLAY, 0);
            }
        }
        return super.onStartCommand(intent, flags, startId);
    }

    // Browsers on MediaBrowserCompat ask for a folder again when told it changed.
    // Those on the framework's MediaBrowser don't subscribe in a way Media3 sees,
    // so they never hear: see listedRoot().
    private void notifyLibraryChanged() {
        if (session == null)
            return;
        CmLog.i(TAG, "the library changed");
        session.notifyChildrenChanged(ROOT_ID, Integer.MAX_VALUE, null);
        for (String folder : knownFolders)
            session.notifyChildrenChanged(folder, Integer.MAX_VALUE, null);
    }

    // Media Center binds and asks the moment the car starts, with the sources
    // still coming up, and a browser that isn't told of a change would stay on
    // an empty list. So an empty root isn't answered until there's something
    // in it, or it's clear there won't be for now: signed out, or no network
    // and nothing saved. A browser shows its spinner meanwhile.
    private ListenableFuture<List<CoreBridge.BrowseItem>> listedRoot() {
        SettableFuture<List<CoreBridge.BrowseItem>> result = SettableFuture.create();
        askRoot(result, android.os.SystemClock.elapsedRealtime() + ROOT_WAIT_MS);
        return result;
    }

    private void askRoot(SettableFuture<List<CoreBridge.BrowseItem>> result, long deadline) {
        ListenableFuture<List<CoreBridge.BrowseItem>> asked = CoreBridge.browse(ROOT_ID, false);
        asked.addListener(() -> {
            List<CoreBridge.BrowseItem> items = Futures.getUnchecked(asked);
            if (!items.isEmpty() || android.os.SystemClock.elapsedRealtime() >= deadline)
                result.set(items);
            else
                new android.os.Handler(getMainLooper()).postDelayed(() -> askRoot(result, deadline), ROOT_POLL_MS);
        }, MoreExecutors.directExecutor());
    }

    // In the order the system takes them, which shows just the first two beside
    // play, next and previous since Android 13: the others are for what shows more,
    // like the car's Media Center, where a tap doesn't get to the app otherwise.
    private ImmutableList<CommandButton> buttons() {
        ImmutableList.Builder<CommandButton> buttons = ImmutableList.builder();
        if (player.dislikeSupported()) {
            buttons.add(new CommandButton.Builder()
                .setDisplayName(getString(R.string.dislike))
                .setIconResId(player.disliked() ? R.drawable.ic_heart_off : R.drawable.ic_heart_off_outline)
                .setSessionCommand(DISLIKE)
                .build());
        }
        if (player.likeSupported()) {
            buttons.add(new CommandButton.Builder()
                .setDisplayName(getString(R.string.like))
                .setIconResId(player.liked() ? R.drawable.ic_favorite : R.drawable.ic_favorite_border)
                .setSessionCommand(LIKE)
                .build());
        }
        buttons.add(new CommandButton.Builder()
            .setDisplayName(getString(R.string.close))
            .setIconResId(R.drawable.ic_close)
            .setSessionCommand(CLOSE)
            .build());
        buttons.add(new CommandButton.Builder()
            .setDisplayName(getString(R.string.open))
            .setIconResId(R.drawable.ic_open)
            .setSessionCommand(OPEN)
            .build());
        return buttons.build();
    }

    private Notification placeholderNotification() {
        NotificationManager manager = getSystemService(NotificationManager.class);
        manager.createNotificationChannel(new NotificationChannel(CHANNEL_ID, "Playback", NotificationManager.IMPORTANCE_LOW));
        return new Notification.Builder(this, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_stat_cloudmus)
            .setContentTitle("CloudMus")
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
        CoreBridge.setLibraryListener(null);
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
            // Everyone may open and close: the notification, the car's media widget.
            SessionCommands commands =
                result.availableSessionCommands.buildUpon().add(OPEN).add(CLOSE).add(LIKE).add(DISLIKE).build();
            return MediaSession.ConnectionResult.accept(commands, result.availablePlayerCommands);
        }

        @Override
        public ListenableFuture<SessionResult> onCustomCommand(MediaSession session,
            MediaSession.ControllerInfo controller, SessionCommand command, Bundle args) {
            if (COMMAND_OPEN.equals(command.customAction)) {
                CmLog.i(TAG, "opening the screen, asked by " + controller.getPackageName());
                try {
                    openScreen.send();
                } catch (PendingIntent.CanceledException error) {
                    CmLog.e(TAG, "can't open the screen", error);
                }
                return Futures.immediateFuture(new SessionResult(SessionResult.RESULT_SUCCESS));
            }
            if (COMMAND_LIKE.equals(command.customAction)) {
                CoreBridge.command(CoreBridge.CMD_LIKE, 0);
                return Futures.immediateFuture(new SessionResult(SessionResult.RESULT_SUCCESS));
            }
            if (COMMAND_DISLIKE.equals(command.customAction)) {
                CoreBridge.command(CoreBridge.CMD_DISLIKE, 0);
                return Futures.immediateFuture(new SessionResult(SessionResult.RESULT_SUCCESS));
            }
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
            return Futures.immediateFuture(LibraryResult.ofItem(folder(ROOT_ID, "CloudMus"), params));
        }

        @Override
        public ListenableFuture<LibraryResult<MediaItem>> onGetItem(MediaLibrarySession session,
            MediaSession.ControllerInfo browser, String mediaId) {
            return Futures.transform(CoreBridge.browse(mediaId, true), items -> items.isEmpty()
                    ? LibraryResult.<MediaItem>ofError(LibraryResult.RESULT_ERROR_BAD_VALUE)
                    : LibraryResult.ofItem(browserItem(items.get(0)), null),
                MoreExecutors.directExecutor());
        }

        @Override
        public ListenableFuture<LibraryResult<ImmutableList<MediaItem>>> onGetChildren(MediaLibrarySession session,
            MediaSession.ControllerInfo browser, String parentId, int page, int pageSize, @Nullable LibraryParams params) {
            CmLog.i(TAG, "children of " + parentId + " for " + browser.getPackageName());
            if (!ROOT_ID.equals(parentId))
                knownFolders.add(parentId);
            ListenableFuture<List<CoreBridge.BrowseItem>> listed = ROOT_ID.equals(parentId)
                ? listedRoot() : CoreBridge.browse(parentId, false);
            return Futures.transform(listed, items -> {
                ImmutableList.Builder<MediaItem> children = ImmutableList.builder();
                // In long: Media3's legacy side asks for a page of MAX_VALUE.
                long from = (long) page * pageSize;
                long to = Math.min(from + pageSize, items.size());
                for (long i = from; i < to; ++i)
                    children.add(browserItem(items.get((int) i)));
                CmLog.i(TAG, "children of " + parentId + ": " + items.size());
                return LibraryResult.ofItemList(children.build(), params);
            }, MoreExecutors.directExecutor());
        }

        // What a browser sends back when one is tapped: an id and no stream,
        // which is all the core wants. Media3 turns down items without a URI
        // unless told otherwise. See CorePlayer.handleSetMediaItems().
        @Override
        public ListenableFuture<List<MediaItem>> onAddMediaItems(MediaSession session,
            MediaSession.ControllerInfo controller, List<MediaItem> mediaItems) {
            return Futures.immediateFuture(mediaItems);
        }

        // "Play" with nothing playing — the steering wheel after the car
        // starts, a Bluetooth headset: continue where it was.
        @Override
        public ListenableFuture<MediaSession.MediaItemsWithStartPosition> onPlaybackResumption(
            MediaSession session, MediaSession.ControllerInfo controller) {
            CmLog.i(TAG, "playback resumption requested by " + controller.getPackageName());
            MediaItem item = new MediaItem.Builder()
                .setMediaId("resume")
                .setMediaMetadata(new MediaMetadata.Builder().setTitle("CloudMus").build())
                .build();
            return Futures.immediateFuture(new MediaSession.MediaItemsWithStartPosition(ImmutableList.of(item), 0, 0));
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

        private MediaItem browserItem(CoreBridge.BrowseItem item) {
            if (item.browsable)
                return folder(item.id, item.title);
            MediaMetadata.Builder metadata = new MediaMetadata.Builder()
                .setTitle(item.title)
                .setSubtitle(item.subtitle)
                .setIsBrowsable(false)
                .setIsPlayable(true)
                .setMediaType(MediaMetadata.MEDIA_TYPE_PLAYLIST);
            // A link, not the picture as with the playing track: Media3 puts
            // pictures into the list, cutting it short at what a binder call takes.
            if (!item.coverFile.isEmpty())
                metadata.setArtworkUri(CoverProvider.uriFor(item.coverFile));
            return new MediaItem.Builder().setMediaId(item.id).setMediaMetadata(metadata.build()).build();
        }
    }
}
