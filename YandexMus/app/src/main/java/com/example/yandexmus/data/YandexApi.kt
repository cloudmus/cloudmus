package com.example.yandexmus.data

import android.util.Xml
import java.security.MessageDigest
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.jsonObject
import okhttp3.FormBody
import okhttp3.HttpUrl.Companion.toHttpUrl
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import org.xmlpull.v1.XmlPullParser
import java.io.StringReader
import java.util.concurrent.TimeUnit

class YandexApi(private val tokens: TokenStore) {
    private val json = Json { ignoreUnknownKeys = true }
    private val http = OkHttpClient.Builder()
        .connectTimeout(20, TimeUnit.SECONDS)
        .readTimeout(40, TimeUnit.SECONDS)
        .followRedirects(true)
        .build()

    var account: Account? = null
        private set

    fun requestDeviceCode(): DeviceCode {
        val body = FormBody.Builder()
            .add("client_id", CLIENT_ID)
            .add("device_id", tokens.deviceId)
            .add("device_name", "YandexMus")
            .build()
        val obj = postForm("$OAUTH/device/code", body, authorized = false)
        return DeviceCode(
            deviceCode = obj.str("device_code") ?: throw ApiException("Нет device_code"),
            userCode = obj.str("user_code") ?: throw ApiException("Нет user_code"),
            verificationUrl = obj.str("verification_url") ?: "https://oauth.yandex.ru/device",
            intervalSec = obj.int("interval") ?: 5,
            expiresInSec = obj.int("expires_in") ?: 300,
        )
    }

    /** null — пользователь ещё не подтвердил вход */
    fun pollDeviceToken(deviceCode: String): Boolean {
        val body = FormBody.Builder()
            .add("grant_type", "device_code")
            .add("code", deviceCode)
            .add("client_id", CLIENT_ID)
            .add("client_secret", CLIENT_SECRET)
            .build()
        val request = Request.Builder().url("$OAUTH/token").post(body).build()
        http.newCall(request).execute().use { response ->
            val text = response.body?.string().orEmpty()
            val obj = runCatching { json.parseToJsonElement(text).jsonObject }.getOrNull()
                ?: throw ApiException("Некорректный ответ OAuth")
            val error = obj.str("error")
            if (error == "authorization_pending" || error == "slow_down") return false
            if (!response.isSuccessful || error != null) {
                throw ApiException(obj.str("error_description") ?: error ?: "Ошибка входа ${response.code}")
            }
            val access = obj.str("access_token") ?: throw ApiException("Нет токена")
            tokens.save(access, obj.str("refresh_token"))
            return true
        }
    }

    fun logout() {
        tokens.clear()
        account = null
    }

    fun loadAccount(): Account {
        val result = getResult("/account/status")
        val acc = result.obj("account") ?: throw ApiException("Нет данных аккаунта")
        val uid = acc.long("uid") ?: throw ApiException("Нет uid")
        val login = acc.str("login") ?: acc.str("displayName") ?: "user"
        val name = acc.str("displayName") ?: login
        return Account(uid, login, name).also { account = it }
    }

    fun searchTracks(query: String, page: Int = 0): List<Track> {
        val url = "$API/search".toHttpUrl().newBuilder()
            .addQueryParameter("text", query)
            .addQueryParameter("type", "track")
            .addQueryParameter("page", page.toString())
            .addQueryParameter("nocorrect", "false")
            .build()
        val result = getResult(url.toString())
        val tracks = result.obj("tracks")?.arr("results") ?: return emptyList()
        return tracks.mapNotNull { (it as? JsonObject)?.let(::parseTrack) }
    }

    fun likedTracks(): List<Track> {
        val uid = requireUid()
        val result = getResult("/users/$uid/likes/tracks?if-modified-since-revision=0")
        val library = result.obj("library") ?: result
        val short = library.arr("tracks") ?: JsonArray(emptyList())
        val ids = short.mapNotNull { el ->
            when (el) {
                is JsonObject -> el.str("id")
                else -> el.idString()
            }
        }.filter { it.isNotBlank() }
        if (ids.isEmpty()) return emptyList()
        return fetchTracks(ids)
    }

    fun fetchTracks(ids: List<String>): List<Track> {
        if (ids.isEmpty()) return emptyList()
        val out = ArrayList<Track>(ids.size)
        ids.chunked(40).forEach { chunk ->
            val body = FormBody.Builder()
                .add("track-ids", chunk.joinToString(","))
                .build()
            val result = postForm("$API/tracks", body)
            val list = when (val unwrapped = result["result"] ?: result) {
                is JsonArray -> unwrapped
                is JsonObject -> unwrapped.arr("result") ?: JsonArray(listOf(unwrapped))
                else -> JsonArray(emptyList())
            }
            list.forEach { el ->
                val obj = el as? JsonObject ?: return@forEach
                parseTrack(obj)?.let(out::add)
            }
        }
        return out
    }

    fun startWave(): WaveBatch {
        val uid = requireUid()
        runCatching {
            radioFeedback(
                station = WAVE_STATION,
                type = "radioStarted",
                from = "mobile-radio-user-$uid",
            )
        }
        return waveTracks(queueTrackId = null)
    }

    fun waveTracks(queueTrackId: String?): WaveBatch {
        val builder = "$API/rotor/station/$WAVE_STATION/tracks".toHttpUrl().newBuilder()
            .addQueryParameter("settings2", "true")
        if (!queueTrackId.isNullOrBlank()) {
            builder.addQueryParameter("queue", queueTrackId)
        }
        val result = getResult(builder.build().toString())
        val batchId = result.str("batchId") ?: result.str("id")
        val sequence = result.arr("sequence") ?: JsonArray(emptyList())
        val tracks = sequence.mapNotNull { el ->
            val obj = el as? JsonObject ?: return@mapNotNull null
            val trackObj = obj.obj("track") ?: obj
            parseTrack(trackObj)
        }
        return WaveBatch(batchId, tracks)
    }

    fun radioFeedback(
        station: String = WAVE_STATION,
        type: String,
        from: String? = null,
        batchId: String? = null,
        trackId: String? = null,
        playedSeconds: Double? = null,
    ) {
        val urlBuilder = "$API/rotor/station/$station/feedback".toHttpUrl().newBuilder()
        if (!batchId.isNullOrBlank()) urlBuilder.addQueryParameter("batch-id", batchId)
        val payload = buildString {
            append("{")
            append("\"type\":\"$type\",")
            append("\"timestamp\":\"${java.time.Instant.now()}\"")
            if (!from.isNullOrBlank()) append(",\"from\":\"$from\"")
            if (!trackId.isNullOrBlank()) append(",\"trackId\":\"$trackId\"")
            if (playedSeconds != null) append(",\"totalPlayedSeconds\":$playedSeconds")
            append("}")
        }
        val request = Request.Builder()
            .url(urlBuilder.build())
            .headers(apiHeaders())
            .post(payload.toRequestBody("application/json; charset=utf-8".toMediaType()))
            .build()
        http.newCall(request).execute().use { it.body?.string() }
    }

    fun resolveStreamUrl(trackId: String): String {
        val raw = getRaw("/tracks/$trackId/download-info")
        val infos = when (val result = raw["result"]) {
            is JsonArray -> result
            is JsonObject -> JsonArray(listOf(result))
            else -> if (raw.str("downloadInfoUrl") != null) JsonArray(listOf(raw)) else JsonArray(emptyList())
        }
        val options = infos.mapNotNull { it as? JsonObject }
        val best = options
            .filter { it.bool("preview") != true }
            .maxByOrNull { info ->
                val bitrate = info.int("bitrateInKbps") ?: 0
                val codecBonus = if (info.str("codec") == "mp3") 1_000 else 0
                bitrate + codecBonus
            } ?: throw ApiException("Нет варианта для проигрывания")
        val xmlUrl = best.str("downloadInfoUrl") ?: throw ApiException("Нет ссылки на файл")
        val xml = rawGet(xmlUrl)
        return buildDirectLink(xml)
    }

    fun downloadToFile(url: String, dest: java.io.File) {
        val request = Request.Builder().url(url).build()
        http.newCall(request).execute().use { response ->
            if (!response.isSuccessful) throw ApiException("Скачивание: ${response.code}")
            val body = response.body ?: throw ApiException("Пустой файл")
            dest.outputStream().use { out -> body.byteStream().copyTo(out) }
        }
    }

    private fun requireUid(): Long =
        account?.uid ?: loadAccount().uid

    private fun getResult(pathOrUrl: String): JsonObject {
        val raw = getRaw(pathOrUrl)
        return (raw["result"] as? JsonObject) ?: raw
    }

    private fun getRaw(pathOrUrl: String): JsonObject {
        val url = if (pathOrUrl.startsWith("http")) pathOrUrl else "$API$pathOrUrl"
        val request = Request.Builder()
            .url(url)
            .headers(apiHeaders())
            .get()
            .build()
        http.newCall(request).execute().use { response ->
            val text = response.body?.string().orEmpty()
            if (response.code == 401) {
                if (refreshToken()) return getRaw(pathOrUrl)
                throw ApiException("Сессия истекла, войдите снова")
            }
            if (!response.isSuccessful) {
                throw ApiException("Ошибка ${response.code}: ${text.take(180)}")
            }
            return json.parseToJsonElement(text).jsonObject
        }
    }

    private fun postForm(url: String, body: FormBody, authorized: Boolean = true): JsonObject {
        val builder = Request.Builder().url(url).post(body)
        if (authorized) builder.headers(apiHeaders())
        http.newCall(builder.build()).execute().use { response ->
            val text = response.body?.string().orEmpty()
            val obj = runCatching { json.parseToJsonElement(text).jsonObject }
                .getOrElse { throw ApiException(text.take(180).ifBlank { "Ошибка ${response.code}" }) }
            if (!response.isSuccessful) {
                throw ApiException(obj.str("error_description") ?: obj.str("error") ?: "Ошибка ${response.code}")
            }
            return obj
        }
    }

    private fun refreshToken(): Boolean {
        val refresh = tokens.refreshToken ?: return false
        val body = FormBody.Builder()
            .add("grant_type", "refresh_token")
            .add("refresh_token", refresh)
            .add("client_id", CLIENT_ID)
            .add("client_secret", CLIENT_SECRET)
            .build()
        val request = Request.Builder().url("$OAUTH/token").post(body).build()
        http.newCall(request).execute().use { response ->
            val text = response.body?.string().orEmpty()
            val obj = runCatching { json.parseToJsonElement(text).jsonObject }.getOrNull() ?: return false
            val access = obj.str("access_token") ?: return false
            tokens.save(access, obj.str("refresh_token") ?: refresh)
            return response.isSuccessful
        }
    }

    private fun apiHeaders(): okhttp3.Headers = okhttp3.Headers.Builder()
        .add("Authorization", "OAuth ${tokens.accessToken.orEmpty()}")
        .add("Accept", "application/json")
        .add("Accept-Language", "ru")
        .add("X-Yandex-Music-Client", "YandexMusicAndroid/24023621")
        .build()

    private fun rawGet(url: String): String {
        val request = Request.Builder().url(url).get().build()
        http.newCall(request).execute().use { response ->
            val text = response.body?.string().orEmpty()
            if (!response.isSuccessful) throw ApiException("XML ${response.code}")
            return text
        }
    }

    private fun buildDirectLink(xml: String): String {
        val parser = Xml.newPullParser()
        parser.setInput(StringReader(xml))
        var host = ""
        var path = ""
        var ts = ""
        var s = ""
        var event = parser.eventType
        while (event != XmlPullParser.END_DOCUMENT) {
            if (event == XmlPullParser.START_TAG) {
                when (parser.name) {
                    "host" -> host = parser.nextText()
                    "path" -> path = parser.nextText()
                    "ts" -> ts = parser.nextText()
                    "s" -> s = parser.nextText()
                }
            }
            event = parser.next()
        }
        if (host.isBlank() || path.isBlank()) throw ApiException("Не удалось получить ссылку")
        val sign = md5(SIGN_SALT + path.drop(1) + s)
        return "https://$host/get-mp3/$sign/$ts$path"
    }

    private fun md5(value: String): String {
        val bytes = MessageDigest.getInstance("MD5").digest(value.toByteArray())
        return bytes.joinToString("") { "%02x".format(it) }
    }

    private companion object {
        const val API = "https://api.music.yandex.net"
        const val OAUTH = "https://oauth.yandex.ru"
        const val CLIENT_ID = "23cabbbdc6cd418abb4b39c32c41195d"
        const val CLIENT_SECRET = "53bc75238f0c4d08a118e51fe9203300"
        const val WAVE_STATION = "user:onyourwave"
        const val SIGN_SALT = "XGRlBW9FXlekgbPrRHuSiA"
    }
}
