package dev.cloudmus.mobile;

import android.content.Context;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.os.Looper;

import androidx.annotation.NonNull;
import androidx.media3.common.C;
import androidx.media3.common.MediaItem;
import androidx.media3.common.MediaMetadata;
import androidx.media3.common.Player;
import androidx.media3.common.util.UnstableApi;
import androidx.media3.common.SimpleBasePlayer;

import com.google.common.collect.ImmutableList;
import com.google.common.util.concurrent.Futures;
import com.google.common.util.concurrent.ListenableFuture;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;

import java.io.ByteArrayOutputStream;
import java.util.List;

// Media3's view of the core: a Player that does no playing itself — the
// commands go to the C++ core (libmpv), its state comes back — so that
// MediaSession, the notification, the media buttons and Android Auto all
// work from the one thing. Audio focus is handled here for the same reason:
// Media3 only does it for its own ExoPlayer.
@UnstableApi
final class CorePlayer extends SimpleBasePlayer implements CoreBridge.Listener {
    private static final String TAG = "CorePlayer";

    private final Context context;
    private final AudioManager audioManager;
    private final AudioFocusRequest focusRequest;

    private String trackId = "";
    private MediaItem trackItem;
    private boolean playing;
    private boolean loading;
    private long positionMs;
    private long durationMs;
    private boolean hasFocus;
    private boolean pausedByFocusLoss;

    CorePlayer(Context context, Looper looper) {
        super(looper);
        this.context = context;
        audioManager = context.getSystemService(AudioManager.class);
        focusRequest = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
            .setAudioAttributes(new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_MEDIA)
                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                .build())
            .setOnAudioFocusChangeListener(this::onFocusChange, new android.os.Handler(looper))
            .build();
    }

    // The core's track and state arrive on its own thread; Player state is the
    // application looper's.
    @Override
    public void onTrack(String id, String title, String artist, String album, long newDurationMs, String coverFile) {
        new android.os.Handler(getApplicationLooper()).post(() -> {
            trackId = id;
            durationMs = newDurationMs;
            trackItem = id.isEmpty() ? null : mediaItem(id, title, artist, album, coverFile);
            invalidateState();
        });
    }

    private static MediaItem mediaItem(String id, String title, String artist, String album, String coverFile) {
        MediaMetadata.Builder metadata = new MediaMetadata.Builder()
            .setTitle(title)
            .setArtist(artist)
            .setAlbumTitle(album)
            .setIsBrowsable(false)
            .setIsPlayable(true);
        byte[] cover = coverFile.isEmpty() ? null : coverBytes(coverFile);
        // Bytes, not a file URI: the controller in the car can't read our files.
        if (cover != null)
            metadata.setArtworkData(cover, MediaMetadata.PICTURE_TYPE_FRONT_COVER);
        return new MediaItem.Builder().setMediaId(id).setMediaMetadata(metadata.build()).build();
    }

    // Small enough for a binder call, which is how the session hands it out.
    private static final int COVER_MAX_SIDE = 512;

    private static byte[] coverBytes(String file) {
        BitmapFactory.Options options = new BitmapFactory.Options();
        options.inJustDecodeBounds = true;
        BitmapFactory.decodeFile(file, options);
        options.inJustDecodeBounds = false;
        options.inSampleSize = 1;
        while (Math.max(options.outWidth, options.outHeight) / options.inSampleSize > COVER_MAX_SIDE)
            options.inSampleSize *= 2;
        Bitmap bitmap = BitmapFactory.decodeFile(file, options);
        if (bitmap == null) {
            CmLog.w(TAG, "can't read the cover " + file);
            return null;
        }
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        bitmap.compress(Bitmap.CompressFormat.JPEG, 85, out);
        bitmap.recycle();
        return out.toByteArray();
    }

    private boolean likeSupported;
    private boolean liked;
    private boolean dislikeSupported;
    private boolean disliked;
    private Runnable feedbackListener;

    boolean likeSupported() {
        return likeSupported;
    }

    boolean liked() {
        return liked;
    }

    boolean dislikeSupported() {
        return dislikeSupported;
    }

    boolean disliked() {
        return disliked;
    }

    void setFeedbackListener(Runnable listener) {
        feedbackListener = listener;
    }

    @Override
    public void onFeedback(boolean newLikeSupported, boolean newLiked, boolean newDislikeSupported, boolean newDisliked) {
        new android.os.Handler(getApplicationLooper()).post(() -> {
            if (likeSupported == newLikeSupported && liked == newLiked && dislikeSupported == newDislikeSupported
                && disliked == newDisliked)
                return;
            likeSupported = newLikeSupported;
            liked = newLiked;
            dislikeSupported = newDislikeSupported;
            disliked = newDisliked;
            if (feedbackListener != null)
                feedbackListener.run();
        });
    }

    @Override
    public void onState(boolean newPlaying, boolean newLoading, long newPositionMs, long newDurationMs) {
        new android.os.Handler(getApplicationLooper()).post(() -> {
            loading = newLoading;
            positionMs = newPositionMs;
            durationMs = newDurationMs;
            // Getting the next track ready is still playing, as far as the
            // system goes.
            if (newPlaying || newLoading) {
                resumeHandler().removeCallbacks(pauseAfterHold);
                setPlaying(true);
            } else if (playing) {
                resumeHandler().removeCallbacks(pauseAfterHold);
                resumeHandler().postDelayed(pauseAfterHold, PAUSE_HOLD_MS);
            }
            invalidateState();
        });
    }

    // The core is paused for a moment at each change of track, before it says
    // it's loading the next. Told to Media3, that took the service out of the
    // foreground, and with the app in the background Android doesn't let it
    // back: a minute later it stopped the service, and the music with it. So a
    // pause only counts once it has lasted.
    private static final long PAUSE_HOLD_MS = 1000;
    private final Runnable pauseAfterHold = () -> {
        setPlaying(false);
        invalidateState();
    };

    private void setPlaying(boolean now) {
        boolean was = playing;
        playing = now;
        if (was == now)
            return;
        CmLog.i(TAG, "core " + (now ? "plays" : "paused"));
        // Playback mostly starts past Media3 — the UI talks to the core
        // directly, autoplay too — so the focus is taken when the core
        // plays, whoever asked. Without it the other player kept the
        // focus, and the car kept its steering-wheel buttons.
        if (now) {
            cancelResume("playing again");
            focusRetries = 0;
            requestFocus();
        }
    }

    // The core alone knows what comes before and after the track (a radio's
    // next one isn't even fetched yet), so around the current item there's a
    // blank on each side: Media3 only passes on "next" and "previous" when it
    // has an item to go to. COMMAND_GET_TIMELINE isn't offered, for controllers
    // not to list the blanks as a queue.
    private static final String BEFORE_ID = "before";
    private static final String AFTER_ID = "after";

    @NonNull
    @Override
    protected State getState() {
        Commands commands = new Commands.Builder()
            .addAll(COMMAND_PLAY_PAUSE, COMMAND_PREPARE, COMMAND_STOP, COMMAND_SEEK_IN_CURRENT_MEDIA_ITEM,
                COMMAND_SEEK_TO_NEXT, COMMAND_SEEK_TO_PREVIOUS, COMMAND_SEEK_TO_NEXT_MEDIA_ITEM,
                COMMAND_SEEK_TO_PREVIOUS_MEDIA_ITEM, COMMAND_SEEK_TO_MEDIA_ITEM, COMMAND_GET_CURRENT_MEDIA_ITEM,
                COMMAND_GET_METADATA, COMMAND_SET_MEDIA_ITEM, COMMAND_RELEASE)
            .build();
        State.Builder state = new State.Builder().setAvailableCommands(commands);
        if (trackItem == null)
            return state.setPlaybackState(STATE_IDLE).build();
        return state.setPlaylist(ImmutableList.of(new MediaItemData.Builder(BEFORE_ID).build(),
                new MediaItemData.Builder(trackId)
                    .setMediaItem(trackItem)
                    .setDurationUs(durationMs > 0 ? durationMs * 1000 : C.TIME_UNSET)
                    .setIsSeekable(true)
                    .build(),
                new MediaItemData.Builder(AFTER_ID).build()))
            .setCurrentMediaItemIndex(1)
            .setPlaybackState(loading ? STATE_BUFFERING : STATE_READY)
            .setPlayWhenReady(playing, PLAY_WHEN_READY_CHANGE_REASON_USER_REQUEST)
            .setContentPositionMs(positionMs)
            .build();
    }

    // A command's effect shows when the core reports it: not a moment of a
    // blank item before that.
    @NonNull
    @Override
    protected State getPlaceholderState(@NonNull State suggested) {
        return getState();
    }

    @NonNull
    @Override
    protected ListenableFuture<?> handleSetPlayWhenReady(boolean playWhenReady) {
        CmLog.i(TAG, "setPlayWhenReady(" + playWhenReady + ")");
        pausedByFocusLoss = false;
        cancelResume(playWhenReady ? "play asked" : "pause asked");
        // Asked for, not a moment between tracks: at once.
        resumeHandler().removeCallbacks(pauseAfterHold);
        playing = playWhenReady;
        if (playWhenReady) {
            requestFocus();
            CoreBridge.command(CoreBridge.CMD_PLAY, 0);
        } else {
            // The focus stays through a pause, as ExoPlayer keeps it: the
            // head unit gives the steering-wheel buttons to whoever holds
            // it, and having let go on pause, "play" never came back to us.
            // Stop and release let it go; another player taking it pauses us.
            CoreBridge.command(CoreBridge.CMD_PAUSE, 0);
        }
        return Futures.immediateVoidFuture();
    }

    @NonNull
    @Override
    protected ListenableFuture<?> handlePrepare() {
        return Futures.immediateVoidFuture();
    }

    @NonNull
    @Override
    protected ListenableFuture<?> handleStop() {
        CoreBridge.command(CoreBridge.CMD_PAUSE, 0);
        abandonFocus();
        return Futures.immediateVoidFuture();
    }

    @NonNull
    @Override
    protected ListenableFuture<?> handleRelease() {
        cancelResume("released");
        abandonFocus();
        return Futures.immediateVoidFuture();
    }

    @NonNull
    @Override
    protected ListenableFuture<?> handleSeek(int mediaItemIndex, long seekPositionMs, @Player.Command int seekCommand) {
        CmLog.i(TAG, "seek: command " + seekCommand + ", item " + mediaItemIndex + ", " + seekPositionMs + " ms");
        switch (seekCommand) {
            case COMMAND_SEEK_TO_NEXT:
            case COMMAND_SEEK_TO_NEXT_MEDIA_ITEM:
                CoreBridge.command(CoreBridge.CMD_NEXT, 0);
                break;
            case COMMAND_SEEK_TO_PREVIOUS:
            case COMMAND_SEEK_TO_PREVIOUS_MEDIA_ITEM:
                CoreBridge.command(CoreBridge.CMD_PREVIOUS, 0);
                break;
            default:
                if (mediaItemIndex == 0)
                    CoreBridge.command(CoreBridge.CMD_PREVIOUS, 0);
                else if (mediaItemIndex == 2)
                    CoreBridge.command(CoreBridge.CMD_NEXT, 0);
                else
                    CoreBridge.command(CoreBridge.CMD_SEEK, Math.max(0, seekPositionMs));
        }
        return Futures.immediateVoidFuture();
    }

    // "Play" with nothing loaded yet comes this way (see the service's
    // onPlaybackResumption()); no items are offered to pick from so far.
    @NonNull
    @Override
    protected ListenableFuture<?> handleSetMediaItems(List<MediaItem> mediaItems, int startIndex, long startPositionMs) {
        CoreBridge.command(CoreBridge.CMD_PLAY, 0);
        requestFocus();
        return Futures.immediateVoidFuture();
    }

    // Resuming after a lost focus. Android has a player stay paused when
    // another one takes the focus for good; in the car that other one is
    // mostly brief — the phone's camera shutter over Bluetooth took it, and
    // the music stopped for good. So: once the other sound has played and
    // nothing plays any more, for a couple of seconds in a row, we take the
    // focus back and play — if that came within the setting's time; past it
    // the other sound is music the user chose. The other sound has to be
    // seen first: the car's radio holds the focus but plays through the
    // tuner, unseen by isMusicActive(), and we cut in on it after two seconds.
    // Any play or pause from the user calls it off.
    private static final long RESUME_POLL_MS = 1000;
    private static final int RESUME_QUIET_POLLS = 2;
    private static final int RESUME_SEEN_WITHIN_POLLS = 3;
    private final Runnable resumePoll = this::pollResume;
    private long resumeDeadline;
    private long focusLostAt;
    private int quietPolls;
    private int polls;
    private boolean otherSoundSeen;

    private void armResume() {
        int seconds = CoreBridge.resumeAfterLossSec(context);
        if (seconds <= 0) {
            CmLog.i(TAG, "focus lost for good: staying paused (resume is off)");
            return;
        }
        focusLostAt = android.os.SystemClock.elapsedRealtime();
        resumeDeadline = focusLostAt + seconds * 1000L;
        quietPolls = 0;
        polls = 0;
        otherSoundSeen = false;
        CmLog.i(TAG, "focus lost for good: resuming if the other sound is over within " + seconds + " s");
        resumeHandler().removeCallbacks(resumePoll);
        resumeHandler().postDelayed(resumePoll, RESUME_POLL_MS);
    }

    private void cancelResume(String why) {
        if (resumeDeadline == 0)
            return;
        resumeDeadline = 0;
        resumeHandler().removeCallbacks(resumePoll);
        CmLog.i(TAG, "resume after focus loss called off: " + why);
    }

    private void pollResume() {
        long now = android.os.SystemClock.elapsedRealtime();
        if (now > resumeDeadline) {
            cancelResume("the other sound went on past the limit");
            return;
        }
        // Ours is paused: whatever plays music is the other one.
        ++polls;
        if (audioManager.isMusicActive()) {
            otherSoundSeen = true;
            quietPolls = 0;
        } else if (!otherSoundSeen) {
            if (polls >= RESUME_SEEN_WITHIN_POLLS) {
                cancelResume("no other sound to wait for: a source we can't see, like the radio");
                return;
            }
        } else if (++quietPolls >= RESUME_QUIET_POLLS) {
            resumeDeadline = 0;
            CmLog.i(TAG, "the other sound is over after " + (now - focusLostAt) / 1000 + " s: resuming");
            requestFocus();
            if (hasFocus)
                CoreBridge.command(CoreBridge.CMD_PLAY, 0);
            return;
        }
        resumeHandler().postDelayed(resumePoll, RESUME_POLL_MS);
    }

    private android.os.Handler resumeHandler;

    private android.os.Handler resumeHandler() {
        if (resumeHandler == null)
            resumeHandler = new android.os.Handler(getApplicationLooper());
        return resumeHandler;
    }

    // Who took the focus, as far as an app may see: the kinds of sound
    // playing (the players' owners are hidden from us).
    private void logActivePlayers() {
        StringBuilder text = new StringBuilder("focus lost for good; players now:");
        for (android.media.AudioPlaybackConfiguration config : audioManager.getActivePlaybackConfigurations()) {
            AudioAttributes attributes = config.getAudioAttributes();
            text.append("\n  usage ").append(attributes.getUsage()).append(", content ")
                .append(attributes.getContentType());
        }
        text.append("\n  music active: ").append(audioManager.isMusicActive());
        CmLog.i(TAG, text.toString());
    }

    // The system may refuse the focus when we ask for it: at boot, with the
    // head unit's own sounds and other players starting at the same time, it
    // did (result 0). The core plays all the same, so we'd play on without
    // the focus — and without the steering-wheel buttons, the car's widget
    // and a pause of the other player. Asking again until it's ours.
    private static final long FOCUS_RETRY_MS = 3000;
    private static final int FOCUS_RETRIES = 20;
    private int focusRetries;
    private final Runnable focusRetry = this::retryFocus;

    private void requestFocus() {
        if (hasFocus)
            return;
        int result = audioManager.requestAudioFocus(focusRequest);
        hasFocus = result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
        CmLog.i(TAG, "audio focus request: " + (hasFocus ? "granted" : "refused (" + result + ")"));
        if (hasFocus) {
            focusRetries = 0;
            resumeHandler().removeCallbacks(focusRetry);
        } else if (result == AudioManager.AUDIOFOCUS_REQUEST_FAILED && focusRetries < FOCUS_RETRIES) {
            resumeHandler().removeCallbacks(focusRetry);
            resumeHandler().postDelayed(focusRetry, FOCUS_RETRY_MS);
        }
    }

    private void retryFocus() {
        if (hasFocus || !playing)
            return;
        ++focusRetries;
        CmLog.i(TAG, "audio focus: try " + (focusRetries + 1));
        requestFocus();
    }

    private void abandonFocus() {
        if (hasFocus) {
            audioManager.abandonAudioFocusRequest(focusRequest);
            hasFocus = false;
            focusRetries = 0;
            resumeHandler().removeCallbacks(focusRetry);
            CmLog.i(TAG, "audio focus abandoned");
        }
    }

    private void onFocusChange(int change) {
        CmLog.i(TAG, "audio focus change: " + change);
        switch (change) {
            case AudioManager.AUDIOFOCUS_GAIN:
                if (pausedByFocusLoss) {
                    pausedByFocusLoss = false;
                    CoreBridge.command(CoreBridge.CMD_PLAY, 0);
                }
                break;
            case AudioManager.AUDIOFOCUS_LOSS:
                hasFocus = false;
                pausedByFocusLoss = false;
                logActivePlayers();
                CoreBridge.command(CoreBridge.CMD_PAUSE, 0);
                if (playing)
                    armResume();
                break;
            case AudioManager.AUDIOFOCUS_LOSS_TRANSIENT:
            case AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK:
                if (playing) {
                    pausedByFocusLoss = true;
                    CoreBridge.command(CoreBridge.CMD_PAUSE, 0);
                }
                break;
            default:
                break;
        }
    }
}
