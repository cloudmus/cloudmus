package com.example.yandexmus

import android.app.Application
import com.example.yandexmus.data.TokenStore
import com.example.yandexmus.data.TrackCache
import com.example.yandexmus.data.YandexApi
import com.example.yandexmus.player.PlayerController

class YandexMusApp : Application() {
    lateinit var tokens: TokenStore
        private set
    lateinit var api: YandexApi
        private set
    lateinit var cache: TrackCache
        private set
    lateinit var player: PlayerController
        private set

    override fun onCreate() {
        super.onCreate()
        tokens = TokenStore(this)
        api = YandexApi(tokens)
        cache = TrackCache(this)
        player = PlayerController(this, api, cache)
    }
}
