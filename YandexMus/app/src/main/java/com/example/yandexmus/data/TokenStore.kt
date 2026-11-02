package com.example.yandexmus.data

import android.content.Context
import android.content.SharedPreferences
import androidx.core.content.edit

class TokenStore(context: Context) {
    private val prefs: SharedPreferences =
        context.getSharedPreferences("yandex_auth", Context.MODE_PRIVATE)

    var accessToken: String?
        get() = prefs.getString(KEY_ACCESS, null)
        set(value) = prefs.edit { putString(KEY_ACCESS, value) }

    var refreshToken: String?
        get() = prefs.getString(KEY_REFRESH, null)
        set(value) = prefs.edit { putString(KEY_REFRESH, value) }

    var deviceId: String
        get() {
            val existing = prefs.getString(KEY_DEVICE, null)
            if (existing != null) return existing
            val created = java.util.UUID.randomUUID().toString().replace("-", "").take(16)
            prefs.edit { putString(KEY_DEVICE, created) }
            return created
        }
        set(value) = prefs.edit { putString(KEY_DEVICE, value) }

    val isLoggedIn: Boolean get() = !accessToken.isNullOrBlank()

    fun save(access: String, refresh: String?) {
        prefs.edit {
            putString(KEY_ACCESS, access)
            putString(KEY_REFRESH, refresh)
        }
    }

    fun clear() {
        prefs.edit {
            remove(KEY_ACCESS)
            remove(KEY_REFRESH)
        }
    }

    private companion object {
        const val KEY_ACCESS = "access_token"
        const val KEY_REFRESH = "refresh_token"
        const val KEY_DEVICE = "device_id"
    }
}
