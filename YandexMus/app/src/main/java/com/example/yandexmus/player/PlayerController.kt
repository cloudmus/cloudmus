package com.example.yandexmus.player

import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.core.content.ContextCompat
import androidx.media3.common.AudioAttributes
import androidx.media3.common.C
import androidx.media3.common.MediaItem
import androidx.media3.common.MediaMetadata
import androidx.media3.common.Player
import androidx.media3.exoplayer.ExoPlayer
import com.example.yandexmus.data.Track
import com.example.yandexmus.data.TrackCache
import com.example.yandexmus.data.YandexApi
import java.io.File
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

data class PlayerState(
    val track: Track? = null,
    val queue: List<Track> = emptyList(),
    val playing: Boolean = false,
    val positionMs: Long = 0,
    val durationMs: Long = 0,
    val loading: Boolean = false,
    val error: String? = null,
    val isWave: Boolean = false,
)

class PlayerController(
    private val context: Context,
    private val api: YandexApi,
    private val cache: TrackCache,
) {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private val _state = MutableStateFlow(PlayerState())
    val state: StateFlow<PlayerState> = _state

    val exoPlayer: ExoPlayer = ExoPlayer.Builder(context)
        .setAudioAttributes(
            AudioAttributes.Builder()
                .setUsage(C.USAGE_MEDIA)
                .setContentType(C.AUDIO_CONTENT_TYPE_MUSIC)
                .build(),
            true,
        )
        .setHandleAudioBecomingNoisy(true)
        .build()
        .also { player ->
            player.addListener(object : Player.Listener {
                override fun onIsPlayingChanged(isPlaying: Boolean) {
                    _state.update { it.copy(playing = isPlaying) }
                }

                override fun onPlaybackStateChanged(playbackState: Int) {
                    if (playbackState == Player.STATE_ENDED) next()
                }

                override fun onPlayerError(error: androidx.media3.common.PlaybackException) {
                    _state.update { it.copy(error = error.message ?: "Ошибка воспроизведения", loading = false) }
                }
            })
        }

    private var prepareJob: Job? = null
    private var progressJob: Job? = null
    private var waveBatchId: String? = null
    private var fetchingWave = false

    init {
        progressJob = scope.launch {
            while (isActive) {
                if (exoPlayer.isPlaying) {
                    _state.update {
                        it.copy(
                            positionMs = exoPlayer.currentPosition,
                            durationMs = exoPlayer.duration.coerceAtLeast(0),
                        )
                    }
                }
                delay(400)
            }
        }
    }

    fun play(tracks: List<Track>, index: Int, isWave: Boolean, batchId: String? = null) {
        if (tracks.isEmpty()) return
        val safeIndex = index.coerceIn(0, tracks.lastIndex)
        if (isWave && batchId != null) waveBatchId = batchId
        _state.value = PlayerState(
            track = tracks[safeIndex],
            queue = tracks,
            playing = false,
            isWave = isWave,
            loading = true,
        )
        startService()
        prepareCurrent()
    }

    fun toggle() {
        if (exoPlayer.isPlaying) exoPlayer.pause() else exoPlayer.play()
    }

    fun seek(positionMs: Long) {
        exoPlayer.seekTo(positionMs)
        _state.update { it.copy(positionMs = positionMs) }
    }

    fun next() {
        val current = _state.value
        if (current.queue.isEmpty()) return
        val idx = currentIndex()
        report("trackFinished")
        if (idx >= current.queue.lastIndex) {
            if (current.isWave) {
                scope.launch { loadMoreWave(playNext = true) }
            }
            return
        }
        play(current.queue, idx + 1, current.isWave, waveBatchId)
        if (current.isWave && idx >= current.queue.lastIndex - 2) {
            scope.launch { loadMoreWave(playNext = false) }
        }
    }

    fun previous() {
        val current = _state.value
        val idx = currentIndex()
        if (idx <= 0) {
            exoPlayer.seekTo(0)
            return
        }
        play(current.queue, idx - 1, current.isWave, waveBatchId)
    }

    fun stop() {
        prepareJob?.cancel()
        exoPlayer.stop()
        exoPlayer.clearMediaItems()
        _state.value = PlayerState()
    }

    fun release() {
        progressJob?.cancel()
        prepareJob?.cancel()
        exoPlayer.release()
    }

    private fun currentIndex(): Int {
        val current = _state.value
        return current.queue.indexOfFirst { it.id == current.track?.id }.coerceAtLeast(0)
    }

    private fun prepareCurrent() {
        val track = _state.value.track ?: return
        prepareJob?.cancel()
        prepareJob = scope.launch {
            _state.update { it.copy(loading = true, error = null) }
            try {
                val item = withContext(Dispatchers.IO) { mediaItemFor(track) }
                exoPlayer.setMediaItem(item)
                exoPlayer.prepare()
                exoPlayer.play()
                _state.update { it.copy(loading = false, playing = true) }
                report("trackStarted")
                withContext(Dispatchers.IO) { cache.touch(track) }
            } catch (e: Exception) {
                _state.update {
                    it.copy(loading = false, error = e.message ?: "Не удалось включить трек")
                }
            }
        }
    }

    private fun mediaItemFor(track: Track): MediaItem {
        val metadata = MediaMetadata.Builder()
            .setTitle(track.title)
            .setArtist(track.artists)
            .setArtworkUri(track.coverUrl?.let(Uri::parse))
            .build()
        if (cache.has(track.id)) {
            return MediaItem.Builder()
                .setUri(Uri.fromFile(cache.fileFor(track.id)))
                .setMediaId(track.id)
                .setMediaMetadata(metadata)
                .build()
        }
        val url = api.resolveStreamUrl(track.id)
        scope.launch(Dispatchers.IO) { saveToCache(track, url) }
        return MediaItem.Builder()
            .setUri(url)
            .setMediaId(track.id)
            .setMediaMetadata(metadata)
            .build()
    }

    private fun saveToCache(track: Track, url: String) {
        val dest = cache.fileFor(track.id)
        val tmp = File(dest.parentFile, dest.name + ".part")
        runCatching {
            api.downloadToFile(url, tmp)
            if (tmp.length() > 16_000) {
                dest.delete()
                tmp.renameTo(dest)
                cache.touch(track)
            } else {
                tmp.delete()
            }
        }.onFailure { tmp.delete() }
    }

    private suspend fun loadMoreWave(playNext: Boolean) {
        if (fetchingWave) return
        fetchingWave = true
        try {
            val lastId = _state.value.queue.lastOrNull()?.id
            val batch = withContext(Dispatchers.IO) { api.waveTracks(lastId) }
            if (batch.batchId != null) waveBatchId = batch.batchId
            val merged = (_state.value.queue + batch.tracks).distinctBy { it.id }
            _state.update { it.copy(queue = merged) }
            if (playNext && batch.tracks.isNotEmpty()) {
                val start = merged.indexOfFirst { it.id == batch.tracks.first().id }.coerceAtLeast(0)
                play(merged, start, isWave = true, batchId = waveBatchId)
            }
        } catch (e: Exception) {
            if (playNext) {
                _state.update { it.copy(error = e.message ?: "Не удалось продолжить волну") }
            }
        } finally {
            fetchingWave = false
        }
    }

    private fun report(type: String) {
        val track = _state.value.track ?: return
        if (!_state.value.isWave) return
        scope.launch(Dispatchers.IO) {
            runCatching {
                api.radioFeedback(
                    type = type,
                    batchId = waveBatchId,
                    trackId = track.id,
                    playedSeconds = exoPlayer.currentPosition / 1000.0,
                )
            }
        }
    }

    private fun startService() {
        val intent = Intent(context, PlaybackService::class.java)
        ContextCompat.startForegroundService(context, intent)
    }
}
