package com.example.yandexmus.ui

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Favorite
import androidx.compose.material.icons.filled.Logout
import androidx.compose.material.icons.filled.Pause
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Save
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.SkipNext
import androidx.compose.material.icons.filled.SkipPrevious
import androidx.compose.material.icons.filled.Waves
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import coil.compose.AsyncImage
import com.example.yandexmus.data.Track
import com.example.yandexmus.player.PlayerState
import java.util.Locale

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AppScreen(vm: AppViewModel) {
    val ui by vm.ui.collectAsStateWithLifecycle()
    val player by vm.player.collectAsStateWithLifecycle()

    if (!ui.ready) {
        Column(
            Modifier.fillMaxSize(),
            verticalArrangement = Arrangement.Center,
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            CircularProgressIndicator()
        }
        return
    }

    if (!ui.loggedIn) {
        LoginScreen(ui = ui, onLogin = vm::startLogin)
        return
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Column {
                        Text("YandexMus")
                        Text(
                            ui.userName,
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                },
                actions = {
                    IconButton(onClick = vm::refresh) {
                        Icon(Icons.Default.Refresh, contentDescription = "Обновить")
                    }
                    IconButton(onClick = vm::logout) {
                        Icon(Icons.Default.Logout, contentDescription = "Выйти")
                    }
                },
            )
        },
        bottomBar = {
            Column {
                MiniPlayer(player, vm)
                NavigationBar {
                    NavigationBarItem(
                        selected = ui.tab == MainTab.Wave,
                        onClick = { vm.selectTab(MainTab.Wave) },
                        icon = { Icon(Icons.Default.Waves, contentDescription = null) },
                        label = { Text("Волна") },
                    )
                    NavigationBarItem(
                        selected = ui.tab == MainTab.Liked,
                        onClick = { vm.selectTab(MainTab.Liked) },
                        icon = { Icon(Icons.Default.Favorite, contentDescription = null) },
                        label = { Text("Сохранённые") },
                    )
                    NavigationBarItem(
                        selected = ui.tab == MainTab.Search,
                        onClick = { vm.selectTab(MainTab.Search) },
                        icon = { Icon(Icons.Default.Search, contentDescription = null) },
                        label = { Text("Поиск") },
                    )
                    NavigationBarItem(
                        selected = ui.tab == MainTab.Cache,
                        onClick = { vm.selectTab(MainTab.Cache) },
                        icon = { Icon(Icons.Default.Save, contentDescription = null) },
                        label = { Text("Кэш") },
                    )
                }
            }
        },
    ) { padding ->
        Column(
            Modifier
                .fillMaxSize()
                .padding(padding),
        ) {
            if (ui.tab == MainTab.Search) {
                OutlinedTextField(
                    value = ui.query,
                    onValueChange = vm::updateQuery,
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 16.dp, vertical = 8.dp),
                    placeholder = { Text("Трек, исполнитель") },
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
                    keyboardActions = KeyboardActions(onSearch = { vm.search() }),
                    trailingIcon = {
                        IconButton(onClick = vm::search) {
                            Icon(Icons.Default.Search, contentDescription = "Искать")
                        }
                    },
                )
            }
            if (ui.tab == MainTab.Wave) {
                Button(
                    onClick = vm::playWaveFromStart,
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
                ) { Text("Слушать мою волну") }
            }
            if (ui.loading) {
                LinearProgressIndicator(Modifier.fillMaxWidth())
            }
            ui.error?.let {
                Text(
                    it,
                    color = MaterialTheme.colorScheme.error,
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
                )
            }
            if (!ui.loading && ui.tracks.isEmpty()) {
                Text(
                    when (ui.tab) {
                        MainTab.Search -> "Введите запрос и нажмите поиск"
                        MainTab.Cache -> "Прослушанные треки появятся здесь"
                        else -> "Пока пусто"
                    },
                    modifier = Modifier.padding(16.dp),
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            LazyColumn(
                modifier = Modifier.fillMaxSize(),
                contentPadding = PaddingValues(bottom = 12.dp),
            ) {
                items(ui.tracks, key = { it.id }) { track ->
                    TrackRow(
                        track = track,
                        playing = player.track?.id == track.id,
                        onClick = { vm.play(track) },
                    )
                }
            }
        }
    }
}

@Composable
private fun LoginScreen(ui: ScreenState, onLogin: () -> Unit) {
    val context = LocalContext.current
    Column(
        Modifier
            .fillMaxSize()
            .padding(24.dp),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text("YandexMus", style = MaterialTheme.typography.headlineLarge, fontWeight = FontWeight.Bold)
        Spacer(Modifier.height(8.dp))
        Text(
            "Простой плеер без рекламы.\nВойдите в свой Яндекс ID.",
            style = MaterialTheme.typography.bodyMedium,
        )
        Spacer(Modifier.height(24.dp))
        if (ui.loading) {
            CircularProgressIndicator()
        } else {
            Button(onClick = onLogin) { Text("Войти через Яндекс") }
        }
        ui.login?.let { login ->
            Spacer(Modifier.height(24.dp))
            Text("Код для входа", style = MaterialTheme.typography.labelLarge)
            Text(
                login.userCode.uppercase(Locale.getDefault()),
                style = MaterialTheme.typography.headlineMedium,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold,
            )
            Spacer(Modifier.height(8.dp))
            Button(onClick = {
                context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(login.url)))
            }) { Text("Открыть страницу Яндекса") }
            TextButton(onClick = {
                val cm = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                cm.setPrimaryClip(ClipData.newPlainText("code", login.userCode))
            }) { Text("Скопировать код") }
            if (login.waiting) {
                Spacer(Modifier.height(12.dp))
                Text("Жду подтверждение в браузере…")
            }
        }
        ui.error?.let {
            Spacer(Modifier.height(12.dp))
            Text(it, color = MaterialTheme.colorScheme.error)
        }
    }
}

@Composable
private fun TrackRow(track: Track, playing: Boolean, onClick: () -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .padding(horizontal = 16.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        AsyncImage(
            model = track.coverUrl,
            contentDescription = null,
            modifier = Modifier
                .size(52.dp)
                .clip(RoundedCornerShape(8.dp)),
            contentScale = ContentScale.Crop,
        )
        Spacer(Modifier.width(12.dp))
        Column(Modifier.weight(1f)) {
            Text(
                track.title,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                fontWeight = if (playing) FontWeight.Bold else FontWeight.Normal,
                color = if (playing) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface,
            )
            Text(
                track.artists,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        Text(
            formatMs(track.durationMs),
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

@Composable
private fun MiniPlayer(state: PlayerState, vm: AppViewModel) {
    val track = state.track ?: return
    Column(
        Modifier
            .fillMaxWidth()
            .padding(horizontal = 8.dp, vertical = 4.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            AsyncImage(
                model = track.coverUrl,
                contentDescription = null,
                modifier = Modifier
                    .size(44.dp)
                    .clip(RoundedCornerShape(6.dp)),
                contentScale = ContentScale.Crop,
            )
            Spacer(Modifier.width(8.dp))
            Column(Modifier.weight(1f)) {
                Text(track.title, maxLines = 1, overflow = TextOverflow.Ellipsis, fontWeight = FontWeight.Medium)
                Text(
                    track.artists,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    style = MaterialTheme.typography.bodySmall,
                )
            }
            IconButton(onClick = vm::previous) {
                Icon(Icons.Default.SkipPrevious, contentDescription = "Назад")
            }
            IconButton(onClick = vm::toggle) {
                Icon(
                    if (state.playing) Icons.Default.Pause else Icons.Default.PlayArrow,
                    contentDescription = "Пауза",
                )
            }
            IconButton(onClick = vm::next) {
                Icon(Icons.Default.SkipNext, contentDescription = "Дальше")
            }
        }
        val duration = state.durationMs.takeIf { it > 0 } ?: track.durationMs
        if (duration > 0) {
            Slider(
                value = state.positionMs.coerceIn(0, duration).toFloat(),
                onValueChange = { vm.seek(it.toLong()) },
                valueRange = 0f..duration.toFloat(),
            )
        }
        state.error?.let {
            Text(it, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall)
        }
    }
}

private fun formatMs(ms: Long): String {
    if (ms <= 0) return ""
    val total = (ms / 1000).toInt()
    val m = total / 60
    val s = total % 60
    return "%d:%02d".format(m, s)
}
