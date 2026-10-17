package com.example.yandexmus.ui

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.example.yandexmus.YandexMusApp
import com.example.yandexmus.data.DeviceCode
import com.example.yandexmus.data.Track
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

enum class MainTab { Wave, Liked, Search, Cache }

data class LoginUi(
    val userCode: String,
    val url: String,
    val waiting: Boolean = true,
)

data class ScreenState(
    val ready: Boolean = false,
    val loggedIn: Boolean = false,
    val userName: String = "",
    val login: LoginUi? = null,
    val tab: MainTab = MainTab.Wave,
    val tracks: List<Track> = emptyList(),
    val loading: Boolean = false,
    val error: String? = null,
    val query: String = "",
)

class AppViewModel(application: Application) : AndroidViewModel(application) {
    private val app = application as YandexMusApp
    private val _ui = MutableStateFlow(ScreenState())
    val ui: StateFlow<ScreenState> = _ui
    val player = app.player.state

    private var pollJob: Job? = null
    private var liked: List<Track> = emptyList()
    private var wave: List<Track> = emptyList()
    private var waveBatchId: String? = null

    init {
        viewModelScope.launch {
            if (app.tokens.isLoggedIn) {
                runCatching { withContext(Dispatchers.IO) { app.api.loadAccount() } }
                    .onSuccess { acc ->
                        _ui.value = ScreenState(
                            ready = true,
                            loggedIn = true,
                            userName = acc.displayName,
                        )
                        loadWave()
                    }
                    .onFailure {
                        app.api.logout()
                        _ui.value = ScreenState(ready = true, loggedIn = false)
                    }
            } else {
                _ui.value = ScreenState(ready = true, loggedIn = false)
            }
        }
    }

    fun startLogin() {
        pollJob?.cancel()
        viewModelScope.launch {
            _ui.update { it.copy(loading = true, error = null) }
            try {
                val code = withContext(Dispatchers.IO) { app.api.requestDeviceCode() }
                _ui.update {
                    it.copy(
                        loading = false,
                        login = LoginUi(code.userCode, code.verificationUrl),
                    )
                }
                pollLogin(code)
            } catch (e: Exception) {
                _ui.update { it.copy(loading = false, error = e.message) }
            }
        }
    }

    fun logout() {
        pollJob?.cancel()
        app.player.stop()
        app.api.logout()
        liked = emptyList()
        wave = emptyList()
        _ui.value = ScreenState(ready = true, loggedIn = false)
    }

    fun selectTab(tab: MainTab) {
        _ui.update { it.copy(tab = tab, error = null) }
        when (tab) {
            MainTab.Wave -> {
                _ui.update { it.copy(tracks = wave) }
                if (wave.isEmpty()) loadWave()
            }
            MainTab.Liked -> {
                _ui.update { it.copy(tracks = liked) }
                if (liked.isEmpty()) loadLiked()
            }
            MainTab.Search -> _ui.update { it.copy(tracks = it.tracks.takeIf { tracks -> _ui.value.query.isNotBlank() } ?: emptyList()) }
            MainTab.Cache -> _ui.update { it.copy(tracks = app.cache.list()) }
        }
    }

    fun refresh() {
        when (_ui.value.tab) {
            MainTab.Wave -> loadWave()
            MainTab.Liked -> loadLiked()
            MainTab.Search -> search()
            MainTab.Cache -> _ui.update { it.copy(tracks = app.cache.list()) }
        }
    }

    fun updateQuery(value: String) {
        _ui.update { it.copy(query = value) }
    }

    fun search() {
        val q = _ui.value.query.trim()
        if (q.isEmpty()) return
        viewModelScope.launch {
            _ui.update { it.copy(loading = true, error = null, tab = MainTab.Search) }
            try {
                val tracks = withContext(Dispatchers.IO) { app.api.searchTracks(q) }
                _ui.update { it.copy(loading = false, tracks = tracks) }
            } catch (e: Exception) {
                _ui.update { it.copy(loading = false, error = e.message) }
            }
        }
    }

    fun play(track: Track, fromList: List<Track> = _ui.value.tracks) {
        val index = fromList.indexOfFirst { it.id == track.id }.coerceAtLeast(0)
        val isWave = _ui.value.tab == MainTab.Wave
        app.player.play(fromList, index, isWave, waveBatchId)
    }

    fun playWaveFromStart() {
        if (wave.isEmpty()) loadWave(play = true) else app.player.play(wave, 0, true, waveBatchId)
    }

    fun toggle() = app.player.toggle()
    fun next() = app.player.next()
    fun previous() = app.player.previous()
    fun seek(position: Long) = app.player.seek(position)

    private fun loadWave(play: Boolean = false) {
        viewModelScope.launch {
            _ui.update { it.copy(loading = true, error = null) }
            try {
                val batch = withContext(Dispatchers.IO) { app.api.startWave() }
                wave = batch.tracks
                waveBatchId = batch.batchId
                _ui.update { it.copy(loading = false, tracks = wave, tab = MainTab.Wave) }
                if (play && wave.isNotEmpty()) {
                    app.player.play(wave, 0, true, waveBatchId)
                }
            } catch (e: Exception) {
                _ui.update { it.copy(loading = false, error = e.message) }
            }
        }
    }

    private fun loadLiked() {
        viewModelScope.launch {
            _ui.update { it.copy(loading = true, error = null) }
            try {
                liked = withContext(Dispatchers.IO) { app.api.likedTracks() }
                _ui.update { it.copy(loading = false, tracks = liked) }
            } catch (e: Exception) {
                _ui.update { it.copy(loading = false, error = e.message) }
            }
        }
    }

    private fun pollLogin(code: DeviceCode) {
        pollJob?.cancel()
        pollJob = viewModelScope.launch {
            val deadline = System.currentTimeMillis() + code.expiresInSec * 1000L
            while (isActive && System.currentTimeMillis() < deadline) {
                delay((code.intervalSec.coerceAtLeast(3) * 1000L))
                val ok = runCatching {
                    withContext(Dispatchers.IO) { app.api.pollDeviceToken(code.deviceCode) }
                }.getOrElse { e ->
                    _ui.update { it.copy(error = e.message, login = it.login?.copy(waiting = false)) }
                    return@launch
                }
                if (ok) {
                    val acc = withContext(Dispatchers.IO) { app.api.loadAccount() }
                    _ui.value = ScreenState(
                        ready = true,
                        loggedIn = true,
                        userName = acc.displayName,
                    )
                    loadWave()
                    return@launch
                }
            }
            _ui.update {
                it.copy(
                    error = "Код истёк, нажмите «Войти» ещё раз",
                    login = it.login?.copy(waiting = false),
                )
            }
        }
    }
}
