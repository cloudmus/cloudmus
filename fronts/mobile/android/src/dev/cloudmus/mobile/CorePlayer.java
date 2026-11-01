package dev.cloudmus.mobile;

import android.content.Context;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.os.Looper;

import androidx.annotation.NonNull;
import androidx.media3.common.C;
import androidx.media3.common.MediaItem;
import androidx.media3.common.Player;
import androidx.media3.common.util.UnstableApi;
import androidx.media3.common.SimpleBasePlayer;

import com.google.common.collect.ImmutableList;
import com.google.common.util.concurrent.Futures;
import com.google.common.util.concurrent.ListenableFuture;

import java.util.ArrayList;
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
    private final List<Tracks.Entry> tracks;
    private final AudioManager audioManager;
    private final AudioFocusRequest focusRequest;

    private int index;
    private boolean playing;
    private boolean loading;
    private long positionMs;
    private long durationMs;
    private boolean hasFocus;
    private boolean pausedByFocusLoss;

    CorePlayer(Context context, Looper looper, List<Tracks.Entry> tracks) {
        super(looper);
        this.context = context;
        this.tracks = tracks;
        audioManager = context.getSystemService(AudioManager.class);
        focusRequest = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
            .setAudioAttributes(new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_MEDIA)
                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                .build())
            .setOnAudioFocusChangeListener(this::onFocusChange, new android.os.Handler(looper))
            .build();
    }

    int trackCount() {
        return tracks.size();
    }

    // The core's state arrives on its own thread; Player state is the
    // application looper's.
    @Override
    public void onState(int newIndex, boolean newPlaying, boolean newLoading, long newPositionMs, long newDurationMs) {
        new android.os.Handler(getApplicationLooper()).post(() -> {
            boolean wasPlaying = playing;
            index = newIndex;
            playing = newPlaying;
            loading = newLoading;
            positionMs = newPositionMs;
            durationMs = newDurationMs;
            if (wasPlaying != playing)
                CmLog.i(TAG, "core " + (playing ? "plays" : "paused") + ", track " + index);
            // Playback mostly starts past Media3 — the UI talks to the core
            // directly, autoplay too — so the focus is taken when the core
            // plays, whoever asked. Without it the other player kept the
            // focus, and the car kept its steering-wheel buttons.
            if (playing && !wasPlaying)
                requestFocus();
            invalidateState();
        });
    }

    @NonNull
    @Override
    protected State getState() {
        Commands commands = new Commands.Builder()
            .addAll(COMMAND_PLAY_PAUSE, COMMAND_PREPARE, COMMAND_STOP, COMMAND_SEEK_IN_CURRENT_MEDIA_ITEM,
                COMMAND_SEEK_TO_NEXT, COMMAND_SEEK_TO_PREVIOUS, COMMAND_SEEK_TO_NEXT_MEDIA_ITEM,
                COMMAND_SEEK_TO_PREVIOUS_MEDIA_ITEM, COMMAND_SEEK_TO_MEDIA_ITEM, COMMAND_GET_CURRENT_MEDIA_ITEM,
                COMMAND_GET_TIMELINE, COMMAND_GET_METADATA, COMMAND_SET_MEDIA_ITEM, COMMAND_RELEASE)
            .build();
        State.Builder state = new State.Builder().setAvailableCommands(commands);
        if (tracks.isEmpty())
            return state.setPlaybackState(STATE_IDLE).build();
        List<MediaItemData> items = new ArrayList<>();
        for (int i = 0; i < tracks.size(); ++i) {
            Tracks.Entry entry = tracks.get(i);
            long duration = i == index && durationMs > 0 ? durationMs : entry.durationMs;
            items.add(new MediaItemData.Builder(entry.id)
                .setMediaItem(entry.item)
                .setDurationUs(duration * 1000)
                .setIsSeekable(true)
                .build());
        }
        return state.setPlaylist(ImmutableList.copyOf(items))
            .setCurrentMediaItemIndex(Math.max(0, Math.min(index, tracks.size() - 1)))
            .setPlaybackState(loading ? STATE_BUFFERING : STATE_READY)
            .setPlayWhenReady(playing, PLAY_WHEN_READY_CHANGE_REASON_USER_REQUEST)
            .setContentPositionMs(positionMs)
            .build();
    }

    @NonNull
    @Override
    protected ListenableFuture<?> handleSetPlayWhenReady(boolean playWhenReady) {
        CmLog.i(TAG, "setPlayWhenReady(" + playWhenReady + ")");
        pausedByFocusLoss = false;
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
                if (mediaItemIndex != index && mediaItemIndex >= 0)
                    CoreBridge.command(CoreBridge.CMD_PLAY_INDEX, mediaItemIndex);
                else
                    CoreBridge.command(CoreBridge.CMD_SEEK, Math.max(0, seekPositionMs));
        }
        return Futures.immediateVoidFuture();
    }

    // Android Auto and the like ask to play an item by id.
    @NonNull
    @Override
    protected ListenableFuture<?> handleSetMediaItems(List<MediaItem> mediaItems, int startIndex, long startPositionMs) {
        if (!mediaItems.isEmpty()) {
            String id = mediaItems.get(Math.max(0, startIndex == C.INDEX_UNSET ? 0 : startIndex)).mediaId;
            for (int i = 0; i < tracks.size(); ++i) {
                if (tracks.get(i).id.equals(id)) {
                    CmLog.i(TAG, "play item " + id + " by request");
                    CoreBridge.command(CoreBridge.CMD_PLAY_INDEX, i);
                    requestFocus();
                }
            }
        }
        return Futures.immediateVoidFuture();
    }

    private void requestFocus() {
        if (hasFocus)
            return;
        int result = audioManager.requestAudioFocus(focusRequest);
        hasFocus = result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
        CmLog.i(TAG, "audio focus request: " + (hasFocus ? "granted" : "refused (" + result + ")"));
    }

    private void abandonFocus() {
        if (hasFocus) {
            audioManager.abandonAudioFocusRequest(focusRequest);
            hasFocus = false;
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
                CoreBridge.command(CoreBridge.CMD_PAUSE, 0);
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
