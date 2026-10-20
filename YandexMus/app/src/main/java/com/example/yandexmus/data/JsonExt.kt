package com.example.yandexmus.data

import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.intOrNull
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.longOrNull

fun JsonElement.asObj(): JsonObject = jsonObject

fun JsonObject.obj(key: String): JsonObject? = this[key] as? JsonObject

fun JsonObject.arr(key: String): JsonArray? = this[key] as? JsonArray

fun JsonObject.str(key: String): String? {
    val el = this[key] ?: return null
    val prim = el as? JsonPrimitive ?: return null
    return prim.contentOrNull?.takeIf { it.isNotBlank() && it != "null" }
}

fun JsonObject.long(key: String): Long? {
    val el = this[key] ?: return null
    val prim = el as? JsonPrimitive ?: return null
    return prim.longOrNull ?: prim.contentOrNull?.toLongOrNull()
}

fun JsonObject.int(key: String): Int? {
    val el = this[key] ?: return null
    val prim = el as? JsonPrimitive ?: return null
    return prim.intOrNull ?: prim.contentOrNull?.toIntOrNull()
}

fun JsonObject.bool(key: String): Boolean? {
    val el = this[key] as? JsonPrimitive ?: return null
    return el.contentOrNull?.toBooleanStrictOrNull() ?: el.content.toBooleanStrictOrNull()
}

fun JsonElement.idString(): String? {
    val prim = this as? JsonPrimitive ?: return (this as? JsonObject)?.str("id")
    return prim.contentOrNull
}

fun parseTrack(obj: JsonObject): Track? {
    val id = obj.str("id") ?: return null
    val title = obj.str("title") ?: obj.str("ogTitle") ?: "Без названия"
    val artists = obj.arr("artists")
        ?.mapNotNull { (it as? JsonObject)?.str("name") }
        ?.joinToString(", ")
        .orEmpty()
        .ifBlank { "Неизвестный исполнитель" }
    val duration = obj.long("durationMs") ?: 0L
    val cover = obj.str("coverUri")
        ?: obj.str("ogImage")
        ?: obj.arr("albums")
            ?.firstOrNull()
            ?.let { it as? JsonObject }
            ?.str("coverUri")
    val albumId = obj.arr("albums")
        ?.firstOrNull()
        ?.let { it as? JsonObject }
        ?.str("id")
    return Track(
        id = id,
        title = title,
        artists = artists,
        durationMs = duration,
        coverUri = cover,
        albumId = albumId,
    )
}
