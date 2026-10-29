package com.example.yandexmus.data

data class Track(
    val id: String,
    val title: String,
    val artists: String,
    val durationMs: Long,
    val coverUri: String?,
    val albumId: String? = null,
) {
    val coverUrl: String?
        get() = coverUri?.takeIf { it.isNotBlank() }?.let { uri ->
            val sized = uri.replace("%%", "200x200")
            if (sized.startsWith("http")) sized else "https://$sized"
        }
}

data class Account(
    val uid: Long,
    val login: String,
    val displayName: String,
)

data class DeviceCode(
    val deviceCode: String,
    val userCode: String,
    val verificationUrl: String,
    val intervalSec: Int,
    val expiresInSec: Int,
)

data class WaveBatch(
    val batchId: String?,
    val tracks: List<Track>,
)

class ApiException(message: String) : RuntimeException(message)
