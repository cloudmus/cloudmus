package com.example.yandexmus

import android.Manifest
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.core.app.ActivityCompat
import com.example.yandexmus.ui.AppScreen
import com.example.yandexmus.ui.AppViewModel
import com.example.yandexmus.ui.theme.YandexMusTheme

class MainActivity : ComponentActivity() {
    private val viewModel: AppViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (Build.VERSION.SDK_INT >= 33) {
            ActivityCompat.requestPermissions(
                this,
                arrayOf(Manifest.permission.POST_NOTIFICATIONS),
                1,
            )
        }
        enableEdgeToEdge()
        setContent {
            YandexMusTheme(darkTheme = false, dynamicColor = true) {
                AppScreen(viewModel)
            }
        }
    }
}
