package com.example.yandexmus.data

import android.content.Context
import java.io.File
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.buildJsonArray
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.put

class TrackCache(context: Context) {
    private val dir = File(context.filesDir, "track_cache").apply { mkdirs() }
    private val indexFile = File(dir, "index.json")
    private val json = Json { ignoreUnknownKeys = true }
    private val lock = Any()

    fun fileFor(trackId: String): File = File(dir, "${safeName(trackId)}.mp3")

    fun has(trackId: String): Boolean {
        val file = fileFor(trackId)
        return file.exists() && file.length() > 16_000
    }

    fun touch(track: Track) {
        synchronized(lock) {
            val items = loadIndex().toMutableList()
            items.removeAll { it.id == track.id }
            items.add(0, CachedTrack(track, System.currentTimeMillis()))
            while (items.size > MAX_TRACKS) {
                val removed = items.removeAt(items.lastIndex)
                fileFor(removed.track.id).delete()
            }
            saveIndex(items)
        }
    }

    fun list(): List<Track> = synchronized(lock) {
        loadIndex()
            .sortedByDescending { it.lastPlayedAt }
            .map { it.track }
            .filter { has(it.id) }
    }

    fun remove(trackId: String) {
        synchronized(lock) {
            fileFor(trackId).delete()
            saveIndex(loadIndex().filterNot { it.id == trackId })
        }
    }

    private fun loadIndex(): List<CachedTrack> {
        if (!indexFile.exists()) return emptyList()
        val root = runCatching {
            json.parseToJsonElement(indexFile.readText()).jsonObject
        }.getOrNull() ?: return emptyList()
        val arr = root["tracks"] as? JsonArray ?: return emptyList()
        return arr.mapNotNull { el ->
            val obj = el as? JsonObject ?: return@mapNotNull null
            val track = parseTrack(
                buildJsonObject {
                    put("id", obj.str("id") ?: return@mapNotNull null)
                    put("title", obj.str("title") ?: "")
                    put("durationMs", JsonPrimitive(obj.long("durationMs") ?: 0L))
                    obj.str("coverUri")?.let { put("coverUri", it) }
                    obj.str("albumId")?.let { put("albumId", it) }
                    put(
                        "artists",
                        buildJsonArray {
                            add(buildJsonObject { put("name", obj.str("artists") ?: "") })
                        },
                    )
                },
            ) ?: return@mapNotNull null
            CachedTrack(track, obj.long("lastPlayedAt") ?: 0L)
        }
    }

    private fun saveIndex(items: List<CachedTrack>) {
        val obj = buildJsonObject {
            put(
                "tracks",
                buildJsonArray {
                    items.forEach { item ->
                        add(
                            buildJsonObject {
                                put("id", item.track.id)
                                put("title", item.track.title)
                                put("artists", item.track.artists)
                                put("durationMs", item.track.durationMs)
                                item.track.coverUri?.let { put("coverUri", it) }
                                item.track.albumId?.let { put("albumId", it) }
                                put("lastPlayedAt", item.lastPlayedAt)
                            },
                        )
                    }
                },
            )
        }
        indexFile.writeText(obj.toString())
    }

    private fun safeName(id: String): String = id.replace(Regex("[^A-Za-z0-9._-]"), "_")

    private data class CachedTrack(val track: Track, val lastPlayedAt: Long) {
        val id: String get() = track.id
    }

    private companion object {
        const val MAX_TRACKS = 400
    }
}
