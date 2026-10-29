package com.example.yandexmus.ui.theme

import android.app.Activity
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.SideEffect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalView
import androidx.core.view.WindowCompat

private val DarkColors = darkColorScheme(
    primary = Yellow,
    onPrimary = Color.Black,
    secondary = YellowDark,
    background = SurfaceDark,
    surface = SurfaceDark,
    surfaceVariant = CardDark,
    onBackground = Color.White,
    onSurface = Color.White,
)

@Composable
fun YandexMusTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    dynamicColor: Boolean = false,
    content: @Composable () -> Unit,
) {
    // ---- ДОБАВЛЯЕМ ЭТОТ БЛОК ДЛЯ СТАТУС-БАРА ----
    val view = LocalView.current
    if (!view.isInEditMode) {
        SideEffect {
            val window = (view.context as Activity).window

            // true — сделает иконки ТЕМНЫМИ (для светлой темы)
            // false — сделает иконки БЕЛЫМИ (для темной темы, чтобы было видно время!)
            WindowCompat.getInsetsController(window, view).isAppearanceLightStatusBars = false
        }
    }
    // --------------------------------------------

    MaterialTheme(
        colorScheme = DarkColors,
        typography = Typography,
        content = content,
    )
}
